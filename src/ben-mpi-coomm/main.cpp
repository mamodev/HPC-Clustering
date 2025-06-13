#include <mpi.h>
#include <iostream>



uint64_t huge_buffer[1024 * 1024 * 256]; // 256 MB buffer



int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    int rank, world_size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);

    if (world_size % 2 != 0) {
        if (rank == 0) 
            std::cerr << "Error: The number of processes must be even." << std::endl;

        MPI_Abort(MPI_COMM_WORLD, 1);
    }   


    for (int mb = 1 ; mb <= 256; mb *= 2) {
        if (rank == 0) {
            std::cout << "Testing with " << mb << " MB buffer." << std::endl;
        }
      
        MPI_Barrier(MPI_COMM_WORLD);


        int acc = 0;

        auto start = MPI_Wtime();
        while (acc < 1024) {

            if (rank < world_size / 2) {
                MPI_Send(huge_buffer, mb, MPI_UINT64_T, rank + world_size / 2, 0, MPI_COMM_WORLD);
            } else {
                MPI_Recv(huge_buffer, mb, MPI_UINT64_T, rank - world_size / 2, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            }

            acc += mb;
        }
        auto end = MPI_Wtime();

        double delta = (end - start);
        double delta_sum = 0.0;
        MPI_Reduce(&delta, &delta_sum, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);

        if (rank == 0) {
            delta_sum /= world_size; 
            // DELTA RAPRESENT TIME TO SEND 1 GB DIVIED BY CHUNKS OF mb MB
            std::cout << "Time to send 1 GB with " << mb << " MB chunks: " << delta << " seconds." << std::endl;
            std::cout << "Throughput: " << (1.0 / delta) * mb << " MB/s" << std::endl;
        }
    }

    MPI_Finalize();
    return 0;
}








