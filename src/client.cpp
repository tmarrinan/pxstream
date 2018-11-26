#include "pxstream/client.h"

PxStream::Client::Client(const char *host, uint16_t port, MPI_Comm comm) :
    _finished(0)
{
    MPI_Comm_dup(comm, &_comm);
    int rc = MPI_Comm_rank(_comm, &_rank);
    rc |= MPI_Comm_size(_comm, &_num_ranks);
    if (rc != 0) {
        fprintf(stderr, "PxStream::Client> Error: could not obtain MPI task ID information\n\n");
        MPI_Abort(_comm, 1);
    }

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
        fprintf(stderr, "PxStream::Client> Warning: machine does not appear to use IEEE 754 floating point format\n");
    }

    // Establish connection to head node server and get ip/port list for rest of nodes
    int i;
    NetSocket::ClientOptions options = NetSocket::CreateClientOptions();
    options.secure = false;
    uint8_t *remote_ip_addresses;
    uint16_t *remote_ports;
    if (_rank == 0)
    {
        Connection conn = {new NetSocket::Client(host, port, options), 0, 0, 0, 0};
        _connections.push_back(conn);
        int server_info_count = 0;
        PxStream::Endian remote_endianness;
        while (server_info_count < 7)
        {
            NetSocket::Client::Event event = conn.client->WaitForNextEvent();
            switch (event.type)
            {
                case NetSocket::Client::EventType::ReceiveBinary:
                    switch (server_info_count)
                    {
                        case 0: // endianness
                            remote_endianness = (PxStream::Endian)(*((uint8_t*)event.binary_data));
                            if (remote_endianness != _endianness)
                            {
                                fprintf(stderr, "PxStream::Client> Warning: remote machine's endianness does not match\n");
                            }
                            delete[] event.binary_data;
                            server_info_count++;
                            break;
                        case 1: // ip addresses
                            _num_remote_ranks = event.data_length / 4;
                            remote_ip_addresses = (uint8_t*)event.binary_data;
                            server_info_count++;
                            break;
                        case 2: // ports
                            remote_ports = (uint16_t*)event.binary_data;
                            for (i = 0; i<_num_remote_ranks; i++)
                            {
                                remote_ports[i] = ntohs(remote_ports[i]);
                            }
                            server_info_count++;
                            break;
                        case 3: // global image width
                            _global_width = ntohl(*((uint32_t*)event.binary_data));
                            server_info_count++;
                            break;
                        case 4: // global image height
                            _global_height = ntohl(*((uint32_t*)event.binary_data));
                            server_info_count++;
                            break;
                        case 5: // image format
                            _px_format = (PixelFormat)(*((uint8_t*)event.binary_data));
                            server_info_count++;
                            break;
                        case 6: // image data type
                            _px_data_type = (PixelDataType)(*((uint8_t*)event.binary_data));
                            server_info_count++;
                            break;
                    }
                    break;
                default:
                    break;
            }
        }
        printf("PxStream::Client> Global Image Size: %ux%u\n", _global_width, _global_height);
    }

    // Share ip/port and image info with other ranks
    MPI_Bcast(&_num_remote_ranks, 1, MPI_INT, 0, MPI_COMM_WORLD);
    if (_rank != 0)
    {
        remote_ip_addresses = new uint8_t[4 * _num_remote_ranks];
        remote_ports = new uint16_t[_num_remote_ranks];
    }
    MPI_Bcast(remote_ip_addresses, 4 * _num_remote_ranks, MPI_UINT8_T, 0, MPI_COMM_WORLD);
    MPI_Bcast(remote_ports, _num_remote_ranks, MPI_UINT16_T, 0, MPI_COMM_WORLD);
    MPI_Bcast(&_global_width, 1, MPI_UINT32_T, 0, MPI_COMM_WORLD);
    MPI_Bcast(&_global_height, 1, MPI_UINT32_T, 0, MPI_COMM_WORLD);
    MPI_Bcast(&_px_format, 1, MPI_UINT8_T, 0, MPI_COMM_WORLD);
    MPI_Bcast(&_px_data_type, 1, MPI_UINT8_T, 0, MPI_COMM_WORLD);

    // Determine which ranks connect to which
    int connections_per_rank = _num_remote_ranks / _num_ranks;
    int connections_extra = _num_remote_ranks % _num_ranks;
    int num_connections = connections_per_rank + (_rank < connections_extra ? 1 : 0);
    int connection_offset = _rank * connections_per_rank + std::min(_rank, connections_extra);

    // Make connections
    NetSocket::Client::Event event;
    for (i = std::max(connection_offset, 1); i < connection_offset + num_connections; i++)
    {
        struct in_addr addr = {*((in_addr_t*)(&(remote_ip_addresses[4*i])))};
        Connection conn = {new NetSocket::Client(inet_ntoa(addr), remote_ports[i], options), 0, 0, 0, 0, NULL, 0};
        do
        {
            event = conn.client->WaitForNextEvent();
            if (event.type == NetSocket::Client::EventType::ReceiveBinary)
            {
                delete[] event.binary_data;
            }
        } while (event.type != NetSocket::Client::EventType::Connect);
        _connections.push_back(conn);
    }

    // Create threads for handling reads
    _read_threads = new std::thread[_connections.size()];
    for (i = 0; i < _connections.size(); i++)
    {
        _read_threads[i] = std::thread(&PxStream::Client::ConnectionRead, this, i);
    }

    // Create and send handshake, and receive connection header (image dims, pixel format, ...)
    uint8_t handshake[13];
    if (_rank == 0)
    {
        struct in_addr ip;
        inet_aton(_connections[0].client->LocalIpAddress().c_str(), &ip);
        uint32_t nrr = htonl(_num_remote_ranks);
        uint64_t cid = PxStream::HToNLL(((uint64_t)ip.s_addr << 32) + (uint64_t)_connections[0].client->LocalPort());
        memcpy(handshake, &nrr, 4);
        memcpy(handshake + 4, &cid, 8);
    }
    MPI_Bcast(handshake, 13, MPI_UINT8_T, 0, MPI_COMM_WORLD);
    handshake[12] = _endianness;
    uint64_t total_pixel_size = 0;
    for (i = 0; i < num_connections; i++)
    {
        _connections[i].client->Send(handshake, 13, NetSocket::CopyMode::MemCopy);
        do
        {
            event = _connections[i].client->WaitForNextEvent();
        } while (event.type != NetSocket::Client::EventType::ReceiveBinary);
        uint32_t *header = reinterpret_cast<uint32_t*>(event.binary_data);
        _connections[i].local_width = header[0];
        _connections[i].local_height = header[1];
        _connections[i].local_offset_x = header[2];
        _connections[i].local_offset_y = header[3];
        _connections[i].pixel_size = (uint32_t)(_connections[i].local_width * _connections[i].local_height * (double)PxStream::GetBitsPerPixel(_px_format, _px_data_type) / 8.0);
        total_pixel_size += _connections[i].pixel_size;
        delete[] event.binary_data;
        printf("PxStream::Client> [rank %d] connected (%ux%u +%u+%u)\n", _rank, _connections[i].local_width, _connections[i].local_height, _connections[i].local_offset_x, _connections[i].local_offset_y);
    }
    _connection_pixel_list = new uint8_t[total_pixel_size];
    uint64_t pixel_list_offset = 0;
    for (i = 0; i < num_connections; i++)
    {
        _connections[i].pixels = (void*)(_connection_pixel_list + pixel_list_offset);
        pixel_list_offset += _connections[i].pixel_size;
    }
}

PxStream::Client::~Client()
{
    //TODO: disconnect client
}

void PxStream::Client::Read()
{
    _read_finished_count = 0;
    _read_condition.notify_all();

    std::unique_lock<std::mutex> lock(_read_mutex);
    while (_read_finished_count < _connections.size())
    {
        _finished_condition.wait(lock);
    }
    lock.unlock();
}

bool PxStream::Client::ServerFinished()
{
    return _finished == _connections.size();
}

void PxStream::Client::GetGlobalDimensions(uint32_t *width, uint32_t *height)
{
    *width = _global_width;
    *height = _global_height;
}

DDR_DataDescriptor* PxStream::Client::CreateGlobalPixelSelection(int32_t *sizes, int32_t *offsets)
{
    int problem_type = DDR_DATA_TYPE_REGULAR_GRID_2D;
    MPI_Datatype type;
    switch (_px_data_type)
    {
        case PixelDataType::Uint8:
            type = MPI_UINT8_T;
            break;
        case PixelDataType::Uint16:
            type = MPI_UINT16_T;
            break;
        case PixelDataType::Uint32:
            type = MPI_UINT32_T;
            break;
        case PixelDataType::Uint64:
            type = MPI_UINT64_T;
            break;
        case PixelDataType::Int8:
            type = MPI_SIGNED_CHAR;
            break;
        case PixelDataType::Int16:
            type = MPI_SHORT;
            break;
        case PixelDataType::Int32:
            type = MPI_INT;
            break;
        case PixelDataType::Int64:
            type = MPI_LONG_LONG;
            break;
        case PixelDataType::Float:
            type = MPI_FLOAT;
            break;
        case PixelDataType::Double:
            type = MPI_DOUBLE;
            break;
    }
    DDR_DataDescriptor *desc = DDR_NewDataDescriptor(_num_ranks, problem_type, type, PxStream::GetDataTypeSize(_px_data_type));

    int i, j;
    int chunks_own = _connections.size();
    int *dims_own = new int[chunks_own * 2];
    int *offsets_own = new int[chunks_own * 2];
    int32_t bpp = PxStream::GetBitsPerPixel(_px_format, _px_data_type);
    for (i = 0; i < _connections.size(); i++)
    {
        dims_own[i * 2 + 0] = _connections[i].local_width * bpp / 8;
        dims_own[i * 2 + 1] = _connections[i].local_height;
        offsets_own[i * 2 + 0] = _connections[i].local_offset_x * bpp / 8;
        offsets_own[i * 2 + 1] = _connections[i].local_offset_y;
    }
    int32_t px_sizes[2] = {sizes[0] * bpp / 8, sizes[1]};
    int32_t px_offsets[2] = {offsets[0] * bpp / 8, offsets[1]};

    DDR_SetupDataMapping(_rank, _num_ranks, chunks_own, dims_own, offsets_own, px_sizes, px_offsets, desc);

    return desc;
}

void PxStream::Client::FillSelection(DDR_DataDescriptor *selection, void *data)
{
    DDR_ReorganizeData(_num_ranks, _connection_pixel_list, data, selection);
}


// Private
void PxStream::Client::ConnectionRead(int connection_idx)
{
    std::unique_lock<std::mutex> lock(_read_mutex, std::defer_lock);
    int read_count;
    bool read_finished;
    bool conn_finished = false;
    uint8_t frame_received_flag = 255;
    while (!conn_finished)
    {
        lock.lock();
        _read_condition.wait(lock);
        lock.unlock();

        read_count = 0;
        read_finished = false;
        while (!read_finished)
        {
            NetSocket::Client::Event event;
            do
            {
                event = _connections[connection_idx].client->WaitForNextEvent();
            } while (event.type != NetSocket::Client::EventType::ReceiveBinary);

            if (read_count == 0)
            {
                if (event.data_length == 1 && *((uint8_t*)event.binary_data) == 1) // next_frame_flag notification
                {
                    read_count++;
                }
                else if (event.data_length == 1 && *((uint8_t*)event.binary_data) == 2) // finished_flag notification
                {
                    _finished++;
                    conn_finished = true;
                    read_finished = true;
                }
            }
            else {
                if (event.data_length == _connections[connection_idx].pixel_size)
                {
                    memcpy(_connections[connection_idx].pixels, event.binary_data, event.data_length);
                }
                else
                {
                    fprintf(stderr, "PxStream::Client> Warning: read length (%u) does not match expected pixel length (%u)\n", event.data_length, _connections[connection_idx].pixel_size);
                }
                _connections[connection_idx].client->Send(&frame_received_flag, 1, NetSocket::CopyMode::MemCopy);
                read_finished = true;
            }
        }
        lock.lock();
        _read_finished_count++;
        lock.unlock();
        _finished_condition.notify_one();
    }
}
