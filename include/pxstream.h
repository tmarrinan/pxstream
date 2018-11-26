#ifndef __PXSTREAM_H_
#define __PXSTREAM_H_

#include <iostream>
#include <arpa/inet.h>

#ifdef __APPLE__
#define __BYTE_ORDER __BYTE_ORDER__
#define __BIG_ENDIAN BIG_ENDIAN
#define __LITTLE_ENDIAN LITTLE_ENDIAN
#endif

#define PXSTREAM_FLOATTEST 1.9961090087890625e2 // IEEE 754 ==> 0x4068F38C80000000
#define PXSTREAM_FLOATBINARY 0x4068F38C80000000LL

namespace PxStream {
    enum PixelDataType : uint8_t {Uint8, Uint16, Uint32, Uint64, Int8, Int16, Int32, Int64, Float, Double};
    enum PixelFormat : uint8_t {RGBA, RGB, GrayScale, YUV444, YUV422, YUV420, DXT1};
    enum Endian : uint8_t {Little, Big};

    class Server;
    class Client;

    uint32_t GetDataTypeSize(PixelDataType type);
    uint32_t GetBitsPerPixel(PixelFormat format, PixelDataType type);
    uint64_t HToNLL(uint64_t val);
    uint64_t NToHLL(uint64_t val);
}

#endif // __PXSTREAM_H_
