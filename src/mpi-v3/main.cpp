#include <mpi.h>
#include "CoresetTree.hpp"
#include "CoresetStream.hpp"
#include "Kmeans.hpp"
#include "parser.hpp"
#include "perf.hpp"
#include <set>
#include <optional>

#if !defined(CORESET_SIZE)
#define CORESET_SIZE 600
#endif

#if !defined(CLUSTERS)
#define CLUSTERS 3
#endif

#if !defined(BATCH_SIZE)
#define BATCH_SIZE 1000
#endif

#if !defined(BATCH_ROUNDS)
#define BATCH_ROUNDS 3
#endif

int main(int argc, char **argv)
{
    MPI_Init(&argc, &argv);

    int world_rank, world_size;
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);

    assert(world_size % 2 == 0 && "Number of processes must be even for this implementation");

    samples_t<float> __samples;
    if (world_rank == 0) {
        auto [samples, path] = parseArgs<float>(argc, argv);
        __samples = std::move(samples);
    }

    size_t features;
    if (world_rank == 0) 
        features = __samples.features;

    MPI_Bcast(&features, 1, MPI_UNSIGNED_LONG, 0, MPI_COMM_WORLD);

    
    size_t __stream_idx = 0;
    while(true) {

        float *data = nullptr;
        size_t real_size = 0;

        if (world_rank == 0) {
            
            // !WARNING: This assumes that the number of samples is divisible by world_size (which could be a limitation when ending the stream)
            // In fact it could happen that the last count is 0; (it should be handled properly)
            if (__stream_idx >= __samples.samples) {
                real_size = 0;
            } else if (CORESET_SIZE * world_size > __samples.samples - __stream_idx) {
                real_size = (__samples.samples - __stream_idx) / world_size * features;
            } else {
                real_size = CORESET_SIZE * features;
            }

            std::cout << "Broadcasting " << real_size << " elements to " << world_size << " processes." << std::endl;
            MPI_Bcast(&real_size, 1, MPI_UNSIGNED_LONG, 0, MPI_COMM_WORLD);
            if (real_size == 0) {
                break;
            }

            
            data = new float[real_size];
            MPI_Scatter(__samples.data.data() + __stream_idx, real_size, MPI_FLOAT, data, real_size, MPI_FLOAT, 0, MPI_COMM_WORLD);
            __stream_idx += CORESET_SIZE * world_size;
        
        } else {
            MPI_Bcast(&real_size, 1, MPI_UNSIGNED_LONG, 0, MPI_COMM_WORLD);
            if (real_size == 0) {
                break; // Exit the loop if no more data is available
            }
        
            if (real_size == 0) {
                MPI_Abort(MPI_COMM_WORLD, 0); //TO implement a proper exit
            }

            data = new float[real_size];
            MPI_Scatter(nullptr, real_size, MPI_FLOAT, data, real_size, MPI_FLOAT, 0, MPI_COMM_WORLD);
        }
    
    
    }

    std::cout << "Process " << world_rank << " finished" << std::endl;
    MPI_Finalize();
}