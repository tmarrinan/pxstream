#include <iostream>
#include <netsocket/server.h>
#include <mpi.h>
#include <ddr.h>

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

    MPI_Finalize();
    
    return 0;
}
