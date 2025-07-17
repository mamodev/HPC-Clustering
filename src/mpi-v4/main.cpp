#include <mpi.h>
#include <iostream>

#include "coreset_stream.hpp"

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    int world_size, world_rank;
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
 
    if (world_rank == 0) {
         master(argc, argv, world_size, world_rank);
    } else {
         worker(argc, argv, world_size, world_rank);
    }

    MPI_Finalize();
    return 0;
}