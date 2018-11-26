#ifndef __PXSTREAM_SERVER_H_
#define __PXSTREAM_SERVER_H_

#include <iostream>
#include <random>
#include <map>
#include <ifaddrs.h>
#include <mpi.h>
#include <netsocket/server.h>
#include "pxstream.h"


class PxStream::Server {
public:
    enum StreamBehavior : uint8_t {WaitForAll, DropFrames};

private:
    enum ClientState : uint8_t {Connecting, Handshake, Streaming, Finished};
    typedef struct Connection {
        uint64_t id;
        ClientState state;
        NetSocket::ClientConnection::Pointer client;
        bool is_new;
        bool has_same_endianness;
        bool ready_to_advance;
    } Connection;

    int _rank;
    int _num_ranks;
    MPI_Comm _comm;

    uint16_t _port;
    uint8_t *_ip_address_list;
    uint16_t *_port_list;
    Endian _endianness;
    StreamBehavior _stream_behavior;
    uint32_t _num_connections;
    uint8_t _connect_header[16];
    NetSocket::Server *_server;

    uint32_t _global_width;
    uint32_t _global_height;
    uint32_t _local_width;
    uint32_t _local_height;
    uint32_t _local_offset_x;
    uint32_t _local_offset_y;
    PixelFormat _px_format;
    PixelDataType _px_data_type;
    void *_pixels;
    uint32_t _pixel_size;

    std::map<std::string, Connection> _connections;

    void GetIpAddress(const char *iface, uint8_t ip_address[4]);
    bool HandleNewConnection(NetSocket::Server::Event& event);

public:
    Server(const char *iface, uint16_t port_min, uint16_t port_max, MPI_Comm comm);
    ~Server();

    void GetMasterIpAddress(char *addr);
    void GetMasterPort(uint16_t *port);
    void Listen(StreamBehavior behavior, uint32_t initial_wait_count);
    void SetImageFormat(PixelFormat format, PixelDataType type);
    void SetGlobalImageSize(uint32_t width, uint32_t height);
    void SetLocalImageSize(uint32_t width, uint32_t height);
    void SetLocalImageOffset(uint32_t x, uint32_t y);
    void SetFrameImage(void *data);
    void Write();
    void AdvanceToNextFrame();
    void Finalize();
};

#endif // __PXSTREAM_SERVER_H_