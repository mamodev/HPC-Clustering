#include <optional>
#include <mpi.h>
#include "CoresetTree.hpp"
#include "CoresetStream.hpp"
#include "Kmeans.hpp"
#include "parser.hpp"
#include "perf.hpp"

#if !defined(CORESET_SIZE)
#define CORESET_SIZE 60000
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
    if (world_size < 2) {
        std::cerr << "This program requires at least 2 MPI processes." << std::endl;
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
    }

    if (world_rank == 0) {
        auto [samples, outPath] = parseArgs<float>(argc, argv);

        // broadcast the number of features to all workers
        for (size_t r = 1; r < world_size; ++r) {
            MPI_Send(&samples.features, 1, MPI_UNSIGNED_LONG, r, 0, MPI_COMM_WORLD);
        }

        size_t num_workers = world_size - 2; // Exclude the master process and the gatherer process
        size_t next_worker = 0;
        for (size_t b = 0; b < samples.samples; b += CORESET_SIZE) {
            size_t batch_size = std::min((size_t) CORESET_SIZE, samples.samples - b);
           
            float *batch = samples.data.data() + b * samples.features;
            size_t batch_bytes = batch_size * samples.features;

            MPI_Send(batch, batch_bytes, MPI_FLOAT, next_worker + 2, 0, MPI_COMM_WORLD);
            next_worker = (next_worker + 1) % num_workers;
        }

        // send termination signal to all workers
        for (size_t r = 2; r < world_size; ++r) {
            MPI_Send(nullptr, 0, MPI_FLOAT, r, 0, MPI_COMM_WORLD);
        }
        std::cout << "Master process finished sending data." << std::endl;

    } else if (world_rank == 1) {
        size_t features;
        MPI_Recv(&features, 1, MPI_UNSIGNED_LONG, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        MPI_Status status;
        int count;
        size_t term_count = 0;

        std::vector<std::optional<Coreset>> coresets;

        // std::cout << "Gatherer process [" << world_rank << "] started receiving data." << std::endl;
        while(true) {
            Coreset coreset(CORESET_SIZE, features);
           
            MPI_Recv(coreset.points.points, CORESET_SIZE * features, MPI_FLOAT, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &status);
            MPI_Get_count(&status, MPI_FLOAT, &count);
            if (count == 0) {
                // std::cout << "Gatherer process [" << world_rank << "] received termination signal from worker " << status.MPI_SOURCE << ".\n";
                term_count++;
                if (term_count == world_size - 2) { // All workers have sent termination signal
                    break;
                }
                
                continue;
            }

            MPI_Recv(coreset.weights, CORESET_SIZE, MPI_FLOAT, status.MPI_SOURCE, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);


            // std::cout << "Gatherer process [" << world_rank << "] received data from worker " << status.MPI_SOURCE << std::endl;

            int idx = -1;

            float *merged_points = nullptr;
            float *merged_weights = nullptr;

            while (idx < (int)coresets.size() - 1) {
                if (!coresets[idx + 1].has_value()) {
                    break;
                }

                idx++;

                if (coresets[idx]->points.points == nullptr) {
                    // std::cout << "FATAL ERROR: Coreset points are null at index " << idx << std::endl;
                    MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
                }

                // std::cout << "Gatherer process [" << world_rank << "] merging coreset with index: " << idx << std::endl;

                if (!merged_points || !merged_weights) {
                    merged_points = (float *) malloc((2 * CORESET_SIZE) * features * sizeof(float));
                    merged_weights = (float *) malloc((2 * CORESET_SIZE) * sizeof(float));
                }

                memcpy(merged_points, coresets[idx]->points.points, CORESET_SIZE * features * sizeof(float));
                memcpy(merged_weights, coresets[idx]->weights, CORESET_SIZE * sizeof(float));
                memcpy(merged_points + CORESET_SIZE * features, coreset.points.points, CORESET_SIZE * features * sizeof(float));
                memcpy(merged_weights + CORESET_SIZE, coreset.weights, CORESET_SIZE * sizeof(float));

                auto root = CoresetTree(merged_points, merged_weights, CORESET_SIZE * 2, features, CORESET_SIZE);
                coreset = std::move(root.extract_coreset());
                coresets[idx] = std::nullopt; 
            }

            if (merged_points && merged_weights) {
                free(merged_points);
                free(merged_weights);
            }

            size_t save_into = idx + 1;
            // std::cout << "Gatherer process [" << world_rank << "] saving coreset into index: " << save_into << std::endl;
            if (save_into >= coresets.size()) {

                coresets.push_back(std::move(coreset));
            } else {
                assert(coresets[save_into].has_value() == false && "Coreset at index should be empty before saving");
                coresets[save_into] = std::move(coreset);
            }
        } 

        std::cout << "Gatherer process [" << world_rank << "] finished receiving data." << std::endl;
        std::cout << "Number of coresets at end: " << coresets.size() << std::endl;
    }  else {
        size_t features;
        MPI_Recv(&features, 1, MPI_UNSIGNED_LONG, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        float *recv_buff = (float *) malloc(2 * CORESET_SIZE * features * sizeof(float));
        bool first_received = false;


        MPI_Status status;
        int count;

        while(true) {
            float *ptr = first_received ? recv_buff + CORESET_SIZE * features : recv_buff;
            MPI_Recv(ptr, CORESET_SIZE * features, MPI_FLOAT, 0, 0, MPI_COMM_WORLD, &status);
            MPI_Get_count(&status, MPI_FLOAT, &count);
            // std::cout << "Worker process [" << world_rank << "] received data with count: " << count << std::endl;
            if (count == 0) {
                break;
            }

            assert(count == CORESET_SIZE * features && "Received data size should match CORESET_SIZE * features");
            
            if (!first_received) {
                first_received = true;
                continue;   
            }

            first_received = false;
            auto ctree = CoresetTree(recv_buff, nullptr, CORESET_SIZE * 2, features, CORESET_SIZE);
            auto coreset = ctree.extract_coreset();

            // std::cout << "Worker process [" << world_rank << "] extracted coreset with size: " << coreset.points.n << std::endl;
            MPI_Send(coreset.points.points, CORESET_SIZE * features, MPI_FLOAT, 1, 0, MPI_COMM_WORLD);
            MPI_Send(coreset.weights, CORESET_SIZE, MPI_FLOAT, 1, 0, MPI_COMM_WORLD);
        }   

        MPI_Send(nullptr, 0, MPI_FLOAT, 1, 0, MPI_COMM_WORLD);


        free(recv_buff);
        std::cout << "Worker process [" << world_rank << "] finished processing data." << std::endl;
    }

    MPI_Finalize();

}