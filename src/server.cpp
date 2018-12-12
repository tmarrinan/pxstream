#include "pxstream/server.h"

PxStream::Server::Server(const char *iface, uint16_t port_min, uint16_t port_max, MPI_Comm comm) :
    _num_connections(0),
    _ip_address_list(NULL),
    _port_list(NULL),
    _server(NULL),
    _global_width(0),
    _global_height(0),
    _local_width(0),
    _local_height(0),
    _local_offset_x(0),
    _local_offset_y(0),
    _px_format(PixelFormat::RGBA),
    _px_data_type(PixelDataType::Uint8)
{
    MPI_Comm_dup(comm, &_comm);
    int rc = MPI_Comm_rank(_comm, &_rank);
    rc |= MPI_Comm_size(_comm, &_num_ranks);
    if (rc != 0) {
        fprintf(stderr, "PxStream::Server> Error: could not obtain MPI task ID information\n");
        MPI_Abort(_comm, 1);
    }

    // Initialize NetSocket server options 
    NetSocket::ServerOptions options = NetSocket::CreateServerOptions();

    // Pick a random open port between `port_min` and `port_max`
    int i;
    int num_ports = port_max - port_min + 1;
    uint16_t *port_options = new uint16_t[num_ports];
    for (i = 0; i < num_ports; i++)
    {
        port_options[i] = port_min + i;
    }
    std::shuffle(port_options, port_options + num_ports, std::default_random_engine(time(0)));
    i = 0;
    bool address_in_use = true;
    do {
        _port = port_options[i];
        try {
            _server = new NetSocket::Server(_port, options);
            address_in_use = false;
        }
        catch (std::exception& e)
        {
            i++;
        }
    } while (address_in_use && i < num_ports);
    delete[] port_options;
    if (address_in_use)
    {
        fprintf(stderr, "PxStream::Server> Error: cannot bind to any port in specified range\n");
        MPI_Abort(_comm, 1);
    }

    // Gather IP address and port information for each rank on rank 0
    uint8_t ip_address[4];
    GetIpAddress(iface, ip_address);
    uint16_t net_port = htons(_port);
    if (_rank == 0)
    {
        _ip_address_list = new uint8_t[4 * _num_ranks];
        _port_list = new uint16_t[_num_ranks];
    }
    MPI_Gather(ip_address, 4, MPI_UINT8_T, _ip_address_list, 4, MPI_UINT8_T, 0, _comm);
    MPI_Gather(&net_port, 1, MPI_UINT16_T, _port_list, 1, MPI_UINT16_T, 0, _comm);

    // Data storage checks - little endian, ieee 754
    if (__BYTE_ORDER == __LITTLE_ENDIAN)
    {
        _endianness = PxStream::Endian::Little;
    }
    else
    {
        _endianness = PxStream::Endian::Big;
    }
    double float_test = PXSTREAM_FLOATTEST;
    int64_t float_bin = *reinterpret_cast<uint64_t*>(&float_test);
    if (float_bin != PXSTREAM_FLOATBINARY)
    {
        fprintf(stderr, "PxStream::Server> Warning: machine does not appear to use IEEE 754 floating point format\n");
    }
}

PxStream::Server::~Server()
{
    // TODO: stop server
}

void PxStream::Server::GetMasterIpAddress(char *addr)
{
    if (_rank == 0)
    {
        struct in_addr ip = {*((in_addr_t*)(_ip_address_list))};
        strcpy(addr, inet_ntoa(ip));
    }
    else
    {
        addr = strcpy(addr, "");
    }
}

void PxStream::Server::GetMasterPort(uint16_t *port)
{
    if (_rank == 0)
    {
        *port = ntohs(_port_list[0]);
    }
    else
    {
        *port = 0;
    }
}

void PxStream::Server::Listen(StreamBehavior behavior, uint32_t initial_wait_count)
{
    _stream_behavior = behavior;
    _pixel_size = (uint32_t)(_local_width * _local_height * (double)PxStream::GetBitsPerPixel(_px_format, _px_data_type) / 8.0);
    memcpy(_connect_header +  0, &_local_width,    4);
    memcpy(_connect_header +  4, &_local_height,   4);
    memcpy(_connect_header +  8, &_local_offset_x, 4);
    memcpy(_connect_header + 12, &_local_offset_y, 4);
    while (_num_connections < initial_wait_count)
    {
        NetSocket::Server::Event event = _server->WaitForNextEvent();
        if (!HandleNewConnection(event))
        {
            // unexpected event ... unless notification that a send finished
            if (event.type == NetSocket::Server::EventType::ReceiveBinary)
            {
                delete[] reinterpret_cast<uint8_t*>(event.binary_data);
            }
            if (event.type != NetSocket::Server::EventType::SendFinished)
            {
                fprintf(stderr, "PxStream::Server> Warning: unexpected event while connecting to clients\n");
            }
        }
    }
}

void PxStream::Server::SetImageFormat(PixelFormat format, PixelDataType type)
{
    _px_format = format;
    _px_data_type = type;
}

void PxStream::Server::SetGlobalImageSize(uint32_t width, uint32_t height)
{
    _global_width = width;
    _global_height = height;
}

void PxStream::Server::SetLocalImageSize(uint32_t width, uint32_t height)
{
    _local_width = width;
    _local_height = height;
}

void PxStream::Server::SetLocalImageOffset(uint32_t x, uint32_t y)
{
    _local_offset_x = x;
    _local_offset_y = y;
}

void PxStream::Server::SetFrameImage(void *data)
{
    _pixels = data;
}

void PxStream::Server::Write()
{
    uint8_t next_frame_flag = 1;
    for (auto& c : _connections)
    {
        c.second.client->Send(&next_frame_flag, 1, NetSocket::CopyMode::MemCopy);
        c.second.client->Send(_pixels, _pixel_size, NetSocket::CopyMode::ZeroCopy);
        c.second.ready_to_advance = false;
    }
}

void PxStream::Server::AdvanceToNextFrame()
{
    if (_stream_behavior == StreamBehavior::WaitForAll)
    {
        uint32_t ready_count = 0;
        std::string event_client_id;
        while (ready_count < _connections.size())
        {
            NetSocket::Server::Event event = _server->WaitForNextEvent();
            if (event.type != NetSocket::Server::EventType::None)
            {
                event_client_id = event.client->Endpoint();
            }
            if (!HandleNewConnection(event))
            {
                switch (event.type)
                {
                    case NetSocket::Server::EventType::ReceiveBinary:
                        if (_connections[event_client_id].state == ClientState::Streaming
                            && event.data_length == 1
                            && reinterpret_cast<uint8_t*>(event.binary_data)[0] == 255)
                        {
                            ready_count++;
                        }
                        delete[] reinterpret_cast<uint8_t*>(event.binary_data);
                        break;
                    default:
                        break;
                }
            }
        }
    }
}

void PxStream::Server::Finalize()
{
    uint8_t finished_flag = 2;
    for (auto& c : _connections)
    {
        c.second.client->Send(&finished_flag, 1, NetSocket::CopyMode::MemCopy);
    }

    uint32_t ready_count = 0;
    std::string event_client_id;
    while (ready_count < _connections.size())
    {
        NetSocket::Server::Event event = _server->WaitForNextEvent();
        if (event.type != NetSocket::Server::EventType::None)
        {
            event_client_id = event.client->Endpoint();
        }
        if (!HandleNewConnection(event))
        {
            switch (event.type)
            {
                case NetSocket::Server::EventType::ReceiveBinary:
                    if (_connections[event_client_id].state == ClientState::Streaming
                        && event.data_length == 1
                        && reinterpret_cast<uint8_t*>(event.binary_data)[0] == 255)
                    {
                        ready_count++;
                    }
                    delete[] reinterpret_cast<uint8_t*>(event.binary_data);
                    break;
                default:
                    break;
            }
        }
    }
    MPI_Barrier(_comm);
}

// Private
void PxStream::Server::GetIpAddress(const char *iface, uint8_t ip_address[4])
{
    struct ifaddrs *interfaces = NULL;
    int success = getifaddrs(&interfaces);
    if (success == 0)
    {
        struct ifaddrs *temp_addr = interfaces;
        while (temp_addr != NULL)
        {
            if(temp_addr->ifa_addr->sa_family == AF_INET)
            {
                if (strcmp(temp_addr->ifa_name, iface) == 0)
                {
                    memcpy(ip_address, &(((struct sockaddr_in*)temp_addr->ifa_addr)->sin_addr), 4);
                    break;
                }
            }
            temp_addr = temp_addr->ifa_next;
        }
    }
    freeifaddrs(interfaces);
}

bool PxStream::Server::HandleNewConnection(NetSocket::Server::Event& event)
{
    bool new_connection_event = false;
    std::string event_client_id;
    uint8_t *data;
    if (event.type != NetSocket::Server::EventType::None)
    {
        event_client_id = event.client->Endpoint();
    }
    switch (event.type)
    {
        case NetSocket::Server::EventType::Connect:
            _connections[event_client_id] = {0, ClientState::Connecting, event.client, true, false, false};
            if (_rank == 0) // initial connection - send server ip addressas and ports for all ranks
            {
                uint32_t net_global_w = htonl(_global_width);
                uint32_t net_global_h = htonl(_global_height);
                event.client->Send(&_endianness, sizeof(uint8_t), NetSocket::CopyMode::ZeroCopy);
                event.client->Send(_ip_address_list, 4 * _num_ranks, NetSocket::CopyMode::ZeroCopy);
                event.client->Send(_port_list, _num_ranks * sizeof(uint16_t), NetSocket::CopyMode::ZeroCopy);
                event.client->Send(&net_global_w, sizeof(uint32_t), NetSocket::CopyMode::MemCopy);
                event.client->Send(&net_global_h, sizeof(uint32_t), NetSocket::CopyMode::MemCopy);
                event.client->Send(&_px_format, sizeof(uint8_t), NetSocket::CopyMode::ZeroCopy);
                event.client->Send(&_px_data_type, sizeof(uint8_t), NetSocket::CopyMode::ZeroCopy);
            }
            // mark as valid event for new connection
            new_connection_event = true;
            break;
        case NetSocket::Server::EventType::ReceiveBinary:
            if (_connections[event_client_id].state == ClientState::Connecting)
            {
                _connections[event_client_id].state = ClientState::Handshake;
                // verify client handshake data is as expected
                data = reinterpret_cast<uint8_t*>(event.binary_data);
                if (event.data_length == 13 && ntohl(*((uint32_t*)data)) == _num_ranks)
                {
                    // store client data
                    _connections[event_client_id].id = PxStream::NToHLL(*((uint64_t*)(data + 4)));
                    _connections[event_client_id].has_same_endianness = data[12] == _endianness;
                    // send connection header
                    event.client->Send(_connect_header, 16, NetSocket::CopyMode::ZeroCopy);
                }
                else // unexpected handshake data
                {
                    fprintf(stderr, "PxStream::Server> Warning: expected handshake (13 byte), received %d bytes instead\n", event.data_length);
                    // TODO: terminate connection
                }
                delete[] reinterpret_cast<uint8_t*>(event.binary_data);
                // mark as valid event for new connection
                new_connection_event = true;
            }
            break;
        case NetSocket::Server::EventType::SendFinished:
            // once connection header is sent, increment verified connections
            if (event.binary_data == _connect_header)
            {
                _connections[event_client_id].state = ClientState::Streaming;
                _connections[event_client_id].ready_to_advance = true;
                printf("PxStream::Server> [rank %d] client %d (%s) connected and verified\n", _rank, _num_connections, event_client_id.c_str());
                _num_connections++;
                // mark as valid event for new connection
                new_connection_event = true;
            }
        default:
            break;
    }
    return new_connection_event;
}
