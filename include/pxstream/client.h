#ifndef __PXSTREAM_CLIENT_H_
#define __PXSTREAM_CLIENT_H_

#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <mpi.h>
extern "C" {
#include <ddr.h>
}
#include <netsocket/client.h>
#include "pxstream.h"

class PxStream::Client {
private:
    typedef struct Connection {
        NetSocket::Client *client;
        uint32_t local_width;
        uint32_t local_height;
        uint32_t local_offset_x;
        uint32_t local_offset_y;
        void *pixels;
        uint32_t pixel_size;
    } Connection;

    int _rank;
    int _num_ranks;
    int _num_remote_ranks;
    MPI_Comm _comm;

    PxStream::Endian _endianness;
    std::vector<Connection> _connections;

    uint32_t _global_width;
    uint32_t _global_height;
    PixelFormat _px_format;
    PixelDataType _px_data_type;
    uint8_t *_begin_read;
    uint32_t _finished;
    uint8_t *_connection_pixel_list[2];
    uint8_t _back_buffer;

    std::thread *_read_threads;
    std::mutex _read_mutex;
    std::condition_variable _read_condition;
    std::condition_variable _finished_condition;
    uint32_t _read_finished_count;

    void ConnectionRead(int connection_idx);

public:
    Client(const char *host, uint16_t port, MPI_Comm comm);
    ~Client();

    void Read();
    bool ServerFinished();
    void GetGlobalDimensions(uint32_t *width, uint32_t *height);
    DDR_DataDescriptor* CreateGlobalPixelSelection(int32_t *sizes, int32_t *offsets);
    void FillSelection(DDR_DataDescriptor *selection, void *data);
};

#endif // __PXSTREAM_CLIENT_H_