#include <iostream>
#include <chrono>
#include <mpi.h>
#include "pxstream/client.h"

uint64_t GetCurrentTime();

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

    // PxStream clients
    if (argc < 3)
    {
        fprintf(stderr, "Error: no host and port provided for PxStream server (rank 0)\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    PxStream::Client stream(argv[1], atoi(argv[2]), MPI_COMM_WORLD);

    int num_frames = 0;
    uint64_t start = GetCurrentTime();

    uint32_t global_width, global_height;
    stream.GetGlobalDimensions(&global_width, &global_height);

    int32_t sizes[2] = {(int32_t)global_width / num_ranks, (int32_t)global_height};
    int32_t offsets[2] = {rank * sizes[0], 0};
    DDR_DataDescriptor *selection = stream.CreateGlobalPixelSelection(sizes, offsets);

    uint32_t img_size = sizes[0] * sizes[1] * 4;
    uint8_t *pixel_list = new uint8_t[img_size * 27];

    while (!stream.ServerFinished())
    {
        stream.Read();
        // process data
        stream.FillSelection(selection, pixel_list + (img_size * num_frames));
        
        MPI_Barrier(MPI_COMM_WORLD);
        num_frames++;
    }
    num_frames--;
    uint64_t end = GetCurrentTime();
    if (rank == 0)
    {
        double elapsed = (double)(end - start) / 1000.0;
        uint64_t overall_data = global_width * global_height * 4LL * 8LL * 26LL;
        double speed = (double)overall_data / elapsed;
        printf("finished - received %d frames in %.3lf secs (%.3lf Mbps)\n", num_frames + 1, (double)(end - start) / 1000.0, speed / (1024.0 * 1024.0));
    }

    
    char filename[64];
    int i;
    sprintf(filename, "pxstream_%02d.ppm", rank);
    FILE *fp = fopen(filename, "wb");
    fprintf(fp, "P6\n%d %d\n255\n", sizes[0], sizes[1]);
    for (i = 0; i < sizes[0] * sizes[1]; i++)
    {
        uint8_t *px = pixel_list + (num_frames * sizes[0] * sizes[1] * 4) + i * 4;
        fprintf(fp, "%c%c%c", px[0], px[1], px[2]);
    }
    fclose(fp);

    MPI_Finalize();
    
    return 0;
}

uint64_t GetCurrentTime()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
