#include <iostream>
#include <string>
#include <vector>
#include <ifaddrs.h>
#include <mpi.h>

#include "pxstream/server.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

typedef struct CompressedImageData {
    unsigned char *data;
    int32_t length;
} CompressedImageData;

typedef struct ImageData {
    unsigned char *data;
    int32_t width;
    int32_t height;
} ImageData;

void GetFrameList(int rank, std::string img_template, int start, int increment, std::vector<std::string> *frame_list);
void GetClosestFactors2(int value, int *factor_1, int *factor_2);
int32_t ReadFile(const char *filename, char **data);

int main(int argc, char **argv)
{
    // initialize MPI
    int rc, rank, num_ranks;
    rc = MPI_Init(&argc, &argv);
    rc |= MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    rc |= MPI_Comm_size(MPI_COMM_WORLD, &num_ranks);
    if (rc != 0)
    {
        fprintf(stderr, "Error initializing MPI and obtaining task ID information\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    // find image sequence
    if (argc < 2)
    {
        fprintf(stderr, "Error: no input image sequence template provided\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    int start = 0;
    int increment = 1;
    bool predecompress = false;
    if (argc >= 3) start = atoi(argv[2]);
    if (argc >= 4) increment = atoi(argv[3]);
    if (argc >= 5) predecompress = strcmp(argv[4], "0") != 0;
    std::vector<std::string> frame_list;
    GetFrameList(rank, argv[1], start, increment, &frame_list);
    MPI_Barrier(MPI_COMM_WORLD);
    if (rank == 0) printf("[ImageStream] Found %d images to stream\n", frame_list.size());

    // split into grid
    int rows;
    int cols;
    GetClosestFactors2(num_ranks, &cols, &rows);
    uint32_t m_col = rank % cols;
    uint32_t m_row = rank / cols;

    // read images into memory
    int i, w, h, c;
    char *img_data;
    int32_t img_len;
    std::vector<CompressedImageData> compressed_images;
    std::vector<ImageData> decompressed_images;
    for (i = 0; i < frame_list.size(); i++)
    {
        img_len = ReadFile(frame_list[i].c_str(), &img_data);
        compressed_images.push_back({reinterpret_cast<unsigned char*>(img_data), img_len});
        if (predecompress)
        {
            ImageData decompressed_img;
            decompressed_img.data = stbi_load_from_memory(compressed_images[i].data, compressed_images[i].length, &w, &h, &c, STBI_rgb_alpha);
            decompressed_img.width = w;
            decompressed_img.height = h;
            decompressed_images.push_back(decompressed_img);
        }
    }

    // decompress first image
    ImageData send_img;
    if (predecompress)
    {
        send_img = decompressed_images[0];
    }
    else
    {
        send_img.data = stbi_load_from_memory(compressed_images[0].data, compressed_images[0].length, &w, &h, &c, STBI_rgb_alpha);
        send_img.width = w;
        send_img.height = h;
    }
    MPI_Barrier(MPI_COMM_WORLD);

    // initialize PxStream
    uint16_t port_min = 8000;
    uint16_t port_max = 8008;
    uint32_t global_width = w * cols;
    uint32_t global_height = h * rows;
    if (rank == 0) printf("[ImageStream] Image load complete (%dx%d)\n", global_width, global_height);
    PxStream::Server stream("lo0", port_min, port_max, MPI_COMM_WORLD);
    stream.SetImageFormat(PxStream::PixelFormat::RGBA, PxStream::PixelDataType::Uint8);
    stream.SetGlobalImageSize(global_width, global_height);
    stream.SetLocalImageSize(w, h);
    stream.SetLocalImageOffset(m_col * w, m_row * h);
    MPI_Barrier(MPI_COMM_WORLD);
    if (rank == 0)
    {
        char master_ip[16];
        uint16_t master_port;
        stream.GetMasterIpAddress(master_ip);
        stream.GetMasterPort(&master_port);
        printf("[ImageStream] Ready for client connections on %s:%u\n", master_ip, master_port);
    }
    stream.Listen(PxStream::Server::StreamBehavior::WaitForAll, 1);

    // stream loop
    MPI_Barrier(MPI_COMM_WORLD);
    if (rank == 0) printf("[ImageStream] Begin stream loop\n");
    for (i = 0; i < frame_list.size(); i++)
    {
        stream.SetFrameImage(send_img.data);
        stream.Write();

        if (i + 1 < frame_list.size())
        {
            if (predecompress)
            {
                send_img = decompressed_images[i + 1];
            }
            else
            {
                send_img.data = stbi_load_from_memory(compressed_images[i + 1].data, compressed_images[i + 1].length, &w, &h, &c, STBI_rgb_alpha);
                send_img.width = w;
                send_img.height = h;
            }
        }

        stream.AdvanceToNextFrame();
    }
    if (rank == 0) printf("all done - goodbye\n");
    stream.Finalize();

    MPI_Finalize();
    
    return 0;
}

void GetFrameList(int rank, std::string img_template, int start, int increment, std::vector<std::string> *frame_list)
{
    std::string img_dir, img_seq, img_base, img_end, img_ext, frame_idx, frame_str, next_img, tmp;
    int i, sep, pos, padding;
    struct stat info;

    sep = img_template.rfind('/');
    if (sep == std::string::npos)
    {
        img_dir = "./";
        img_seq = img_template;
    }
    else
    {
        img_dir = img_template.substr(0, sep + 1);
        img_seq = img_template.substr(sep + 1);
    }
    if (stat(img_dir.c_str(), &info) != 0 || !(info.st_mode & S_IFDIR))
    {
        fprintf(stderr, "Error: %s is not a directory\n", img_dir.c_str());
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    pos = img_seq.find("%d");
    if (pos != std::string::npos)
    {
        padding = 0;
    }
    else
    {
        pos = img_seq.find("%0");
        if (pos != std::string::npos)
        {
            padding = atoi(img_seq.substr(pos + 1).c_str());
        }
        else
        {
            fprintf(stderr, "Error: must specify image sequence template (%%d or %%0Nd)\n");
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
    }

    img_base = img_seq.substr(0, pos);
    tmp = img_seq.substr(pos);
    pos = tmp.find('d');
    if (pos == std::string::npos)
    {
        fprintf(stderr, "Error: must specify image sequence template (%%d or %%0Nd)\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    img_end = tmp.substr(pos + 1);
    pos = img_end.rfind(".");
    if (pos != std::string::npos)
    {
        img_ext = img_end.substr(pos);
        img_end = img_end.substr(0, pos);
    }
    else
    {
        fprintf(stderr, "Error: no image extension provided\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    i = 0;
    do
    {
        frame_idx = std::to_string(start + (i * increment));
        if (padding > 0)
        {
            tmp = std::string(padding, '0').append(frame_idx);
            frame_str = tmp.substr(tmp.length() - padding);
        }
        else
        {
            frame_str = frame_idx;
        }
        next_img = img_dir + img_base + frame_str + img_end + "_" + std::to_string(rank) + img_ext;
        frame_list->push_back(next_img);
        i++;
    } while (stat(next_img.c_str(), &info) == 0 && !(info.st_mode & S_IFDIR));
    frame_list->pop_back();
}

void GetClosestFactors2(int value, int *factor_1, int *factor_2)
{
    int test_num = (int)sqrt(value);
    while (value % test_num != 0)
    {
        test_num--;
    }
    *factor_2 = test_num;
    *factor_1 = value / test_num;
}

int32_t ReadFile(const char *filename, char **data)
{
    FILE *fp = fopen(filename, "rb");
    if (fp == NULL)
    {
        fprintf(stderr, "Error: cannot open %s\n", filename);
        return -1;
    }

    fseek(fp, 0, SEEK_END);
    int32_t fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    *data = (char*)malloc(fsize);
    size_t read = fread(*data, fsize, 1, fp);
    if (read != 1)
    {
        fprintf(stderr, "Error: cannot read %s\n", filename);
        return -1;
    }
    fclose(fp);

    return fsize;
}
