#include "pxstream.h"

uint32_t PxStream::GetDataTypeSize(PixelDataType type)
{
    uint32_t size = 0;
    switch (type)
    {
        case PixelDataType::Int8:
        case PixelDataType::Uint8:
            size = 1;
            break;
        case PixelDataType::Int16:
        case PixelDataType::Uint16:
            size = 2;
            break;
        case PixelDataType::Int32:
        case PixelDataType::Uint32:
        case PixelDataType::Float:
            size = 4;
            break;
        case PixelDataType::Int64:
        case PixelDataType::Uint64:
        case PixelDataType::Double:
            size = 8;
            break;
    }
    return size;
}

uint32_t PxStream::GetBitsPerPixel(PixelFormat format, PixelDataType type)
{
    uint32_t size = 0;
    switch (format)
    {
        case PixelFormat::RGBA:
            size = 4 * GetDataTypeSize(type) * 8;
            break;
        case PixelFormat::RGB:
            size = 3 * GetDataTypeSize(type) * 8;
            break;
        case PixelFormat::GrayScale:
            size = GetDataTypeSize(type) * 8;
            break;
        case PixelFormat::YUV444:
            break;
        case PixelFormat::YUV422:
            break;
        case PixelFormat::YUV420:
            break;
        case PixelFormat::DXT1:
            break;
    }
    return size;
}

uint64_t PxStream::HToNLL(uint64_t val)
{
#if __BYTE_ORDER == __BIG_ENDIAN
    return val; 
#else
    uint64_t rval;
    uint8_t *data = (uint8_t *)&rval;

    data[0] = val >> 56;
    data[1] = val >> 48;
    data[2] = val >> 40;
    data[3] = val >> 32;
    data[4] = val >> 24;
    data[5] = val >> 16;
    data[6] = val >> 8;
    data[7] = val >> 0;

    return rval;
#endif
}

uint64_t PxStream::NToHLL(uint64_t val)
{
#if __BYTE_ORDER == __BIG_ENDIAN
    return val;
#else
    uint64_t rval;
    uint8_t *data = (uint8_t *)&rval;

    data[0] = val >> 56;
    data[1] = val >> 48;
    data[2] = val >> 40;
    data[3] = val >> 32;
    data[4] = val >> 24;
    data[5] = val >> 16;
    data[6] = val >> 8;
    data[7] = val >> 0;

    return rval;
#endif
}

