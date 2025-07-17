#include <mpi.h>
#include <iostream>

#include "parser.hpp"
#include "coreset.hpp"


int master(int argc, char** argv, int world_size, int world_rank) {
   
    auto [samples, outDir, coreset_size] = parseArgs<float>(argc, argv);
    size_t features_size = samples.features;
    size_t batch_size = 2 * coreset_size; // each worker will process 2x the coreset size
    MPI_Bcast(&coreset_size, 1, MPI_UNSIGNED_LONG, 0, MPI_COMM_WORLD);
    MPI_Bcast(&features_size, 1, MPI_UNSIGNED_LONG, 0, MPI_COMM_WORLD);
    MPI_Bcast(&batch_size, 1, MPI_UNSIGNED_LONG, 0, MPI_COMM_WORLD);


    return 0;
}

int worker(int argc, char** argv, int world_size, int world_rank) {
    size_t coreset_size, features_size, batch_size;
    MPI_Bcast(&coreset_size, 1, MPI_UNSIGNED_LONG, 0, MPI_COMM_WORLD);
    MPI_Bcast(&features_size, 1, MPI_UNSIGNED_LONG, 0, MPI_COMM_WORLD);
    MPI_Bcast(&batch_size, 1, MPI_UNSIGNED_LONG, 0, MPI_COMM_WORLD);

    std::cout << "Worker " << world_rank << " started with coreset size: " << coreset_size 
              << ", features size: " << features_size 
              << ", batch size: " << batch_size << std::endl;

    std::vector<float> buff1(batch_size * (features_size + 1));
    // std::vector<float> buff2(batch_size * (features_size + 1));

    MPI_Status status;

    std::vector<std::vector<float>> buckets();

    while (true) {
        MPI_Sendrecv( 
            nullptr, 0, MPI_FLOAT, 0, 0,
            buff1.data(), buff1.size(), MPI_FLOAT, MPI_ANY_SOURCE, MPI_ANY_TAG,
            MPI_COMM_WORLD, &status
        );

        int count;
        MPI_Get_count(&status, MPI_FLOAT, &count);
        if (count == 0) {
            std::cout << "Worker " << world_rank << " received no data, exiting." << std::endl;
            break; // No more data to process
        }

        int rank = status.MPI_TAG;
        int real_size = count / (features_size + 1);
        fassert(rank == 0, "Worker received data from unexpected rank");

    }

    return 0;
}


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