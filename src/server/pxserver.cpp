#include <iostream>
#include <string>
#include <vector>
#include <netsocket/server.h>
#include <mpi.h>
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

void OnConnection(NetSocket::Server& server, NetSocket::ClientConnection::Pointer client);
void OnDisonnect(NetSocket::Server& server, std::string client_endpoint);
void OnStringMessage(NetSocket::Server& server, NetSocket::ClientConnection::Pointer client, std::string message);
void GetFrameList(std::string img_template, int start, int increment, std::vector<std::string> *frame_list);
void GetImageReadIndices(int num_images);
int FindImageOwner(int idx);
int32_t ReadFile(const char *filename, char **data);

std::vector<CompressedImageData> compressed_images;
ImageData send_img;
int rank, num_ranks, img_idx;
int *img_start_idx, *img_end_idx, *scatter_counts, *scatter_offsets;
unsigned char *scatter_recv;

int main(int argc, char **argv)
{
    // initialize MPI
    int rc;
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
    if (argc >= 3) start = atoi(argv[2]);
    if (argc >= 4) increment = atoi(argv[3]);
    std::vector<std::string> frame_list;
    GetFrameList(argv[1], start, increment, &frame_list);

    img_start_idx = new int[num_ranks];
    img_end_idx = new int[num_ranks];
    GetImageReadIndices(frame_list.size());
    printf("Rank %d: start %d, end %d\n", rank, img_start_idx[rank], img_end_idx[rank]);

    // read images into memory
    img_idx = 0;
    int i, w, h, c;
    char *img_data;
    int32_t img_len;
    for (i = img_start_idx[rank]; i <= img_end_idx[rank]; i++)
    {
        img_len = ReadFile(frame_list[i].c_str(), &img_data);
        compressed_images.push_back({reinterpret_cast<unsigned char*>(img_data), img_len});
    }
    send_img.data = stbi_load_from_memory(compressed_images[0].data, compressed_images[0].length, &w, &h, &c, STBI_rgb_alpha);
    send_img.width = w;
    send_img.height = h;
    int rows_per_rank = send_img.height / num_ranks;
    int rows_extra = send_img.height % num_ranks;
    scatter_counts = new int[num_ranks];
    scatter_offsets = new int[num_ranks];
    int offset = 0;
    for (i = 0; i < num_ranks; i++)
    {
        scatter_offsets[i] = offset;
        offset += rows_per_rank * 4;
        if (i < rows_extra)
        {
            offset++;
        }
        scatter_counts[i] = offset - scatter_offsets[i];
    }
    scatter_recv = new unsigned char[scatter_counts[rank]];

    // create NetSocket server (1 per rank)
    uint16_t port = 8000 + rank;
    NetSocket::ServerOptions options = NetSocket::CreateServerOptions();
    options.flags = NetSocket::GeneralFlags::None;
    NetSocket::Server server(port, options);
    server.ConnectCallback(OnConnection);
    server.DisconnectCallback(OnDisonnect);
    server.Run();

    MPI_Finalize();
    
    return 0;
}

void OnConnection(NetSocket::Server& server, NetSocket::ClientConnection::Pointer client)
{
    client->ReceiveStringCallback(OnStringMessage);

    // wait for all clients to connect
    // assume fewer clients than servers
    //  - each client connects to multiple servers
    //  - each server has only 1 client
    MPI_Barrier(MPI_COMM_WORLD);

    if (img_idx >= img_start_idx[rank] && img_idx <= img_end_idx[rank])
    {
        MPI_Scatterv(send_img.data, scatter_counts, scatter_offsets, MPI_UNSIGNED_CHAR, scatter_recv, scatter_counts[rank], MPI_UNSIGNED_CHAR, rank, MPI_COMM_WORLD);
        client->Send(scatter_recv, scatter_counts[rank], NetSocket::CopyMode::ZeroCopy);

        free(send_img.data);
        if (img_idx + 1 < compressed_images.size())
        {
            int w, h, c;
            int compress_idx = img_idx + 1 - img_start_idx[rank];
            send_img.data = stbi_load_from_memory(compressed_images[compress_idx].data, compressed_images[compress_idx].length, &w, &h, &c, STBI_rgb_alpha);
            send_img.width = w;
            send_img.height = h;
        }
    }
    else
    {
        int root = FindImageOwner(img_idx);
        MPI_Scatterv(NULL, scatter_counts, scatter_offsets, MPI_UNSIGNED_CHAR, scatter_recv, scatter_counts[rank], MPI_UNSIGNED_CHAR, root, MPI_COMM_WORLD);
        client->Send(scatter_recv, scatter_counts[rank], NetSocket::CopyMode::ZeroCopy);
    }

    img_idx++;
}

void OnDisonnect(NetSocket::Server& server, std::string client_endpoint)
{

}

void OnStringMessage(NetSocket::Server& server, NetSocket::ClientConnection::Pointer client, std::string message)
{
    if (message == "ready")
    {
        if (img_idx > img_end_idx[num_ranks - 1])
        {
            // TODO: terminate server
        }
        else
        {
            if (img_idx >= img_start_idx[rank] && img_idx <= img_end_idx[rank])
            {
                MPI_Scatterv(send_img.data, scatter_counts, scatter_offsets, MPI_UNSIGNED_CHAR, scatter_recv, scatter_counts[rank], MPI_UNSIGNED_CHAR, rank, MPI_COMM_WORLD);
                client->Send(scatter_recv, scatter_counts[rank], NetSocket::CopyMode::ZeroCopy);

                free(send_img.data);
                if (img_idx + 1 < compressed_images.size())
                {
                    int w, h, c;
                    int compress_idx = img_idx + 1 - img_start_idx[rank];
                    send_img.data = stbi_load_from_memory(compressed_images[compress_idx].data, compressed_images[compress_idx].length, &w, &h, &c, STBI_rgb_alpha);
                    send_img.width = w;
                    send_img.height = h;
                }
            }
            else
            {
                int root = FindImageOwner(img_idx);
                MPI_Scatterv(NULL, scatter_counts, scatter_offsets, MPI_UNSIGNED_CHAR, scatter_recv, scatter_counts[rank], MPI_UNSIGNED_CHAR, root, MPI_COMM_WORLD);
                client->Send(scatter_recv, scatter_counts[rank], NetSocket::CopyMode::ZeroCopy);
            }

            img_idx++;
        }
    }
}

void GetFrameList(std::string img_template, int start, int increment, std::vector<std::string> *frame_list)
{
    std::string img_dir, img_seq, img_base, img_end, frame_idx, frame_str, next_img, tmp;
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
        next_img = img_dir + img_base + frame_str + img_end;
        frame_list->push_back(next_img);
        i++;
    } while (stat(next_img.c_str(), &info) == 0 && !(info.st_mode & S_IFDIR));
    frame_list->pop_back();
}

void GetImageReadIndices(int num_images)
{
    int i;
    int img_per_rank = num_images / num_ranks;
    int img_extra = num_images % num_ranks;
    int start_idx = 0;
    for (i = 0; i < num_ranks; i++)
    {
        img_start_idx[i] = start_idx;
        start_idx += img_per_rank;
        if (i < img_extra)
        {
            start_idx++;
        }
        img_end_idx[i] = start_idx - 1;
    }
}

int FindImageOwner(int idx)
{
    int i;
    for (i = 0; i < num_ranks; i++)
    {
        if (idx >= img_start_idx[i] && idx <= img_end_idx[i])
        {
            return i;
        }
    }
    return -1;
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
