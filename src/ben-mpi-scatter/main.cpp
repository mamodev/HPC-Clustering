#include <mpi.h>
#include <iostream>
#include <chrono>
#include <random>
#include <thread>

#include "assert.hpp"
#include "parser.hpp"

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    int world_size;
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);

    int world_rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);


    assert(world_size >= 2, "This program requires at least two processes.");


    auto start = std::chrono::high_resolution_clock::now();

    std::string loc = std::string(__FILE__) + ":" + std::to_string(__LINE__);

    std::string usage = "Usage: <cmd> <stream_size> <master_type> <work_time> <work_std_dev>\n" 
                        "master_type can be one of: async1, async2, sync, pull-sync, pull-async (this list might be not inline with code)\n" 
                        "stream_size is the total number of data points to be sent by the master process.\n" 
                        "work_time is the time each worker process will simulate work (in seconds).\n"  
                        "work_std_dev is the standard deviation of the work time (in seconds).\n"
                        "Code at: " + loc;

    if (argc < 5) {     
        if (world_rank == 0) 
            std::cerr << usage;
            MPI_Abort(MPI_COMM_WORLD, 1);
    }

    size_t stream_size = std::stoull(argv[1]);
    std::string master_type = argv[2];
    double work_time = std::stod(argv[3]);
    double work_std_dev = std::stod(argv[4]);

    if (world_rank == 0 ) {

        size_t coreset_size = 10000; // Default coreset size
        size_t features = 10; // Default number of features

        float * buff = new float[coreset_size * features];
        
        MPI_Bcast(&coreset_size, 1, MPI_UINT64_T, 0, MPI_COMM_WORLD);
        MPI_Bcast(&features, 1, MPI_UINT64_T, 0, MPI_COMM_WORLD);

        start = std::chrono::high_resolution_clock::now();

        assert(stream_size > (world_size - 1), "Stream size must be greater than the number of worker processes.");

        if (master_type == "async1") {
            int sent = 0;
            MPI_Request request[world_size - 1];
            int indices[world_size - 1];

            while (sent < stream_size) {
                int local_count = 0;
                for (int i = 1; i < world_size; ++i) {
                    if (sent >= stream_size) {
                        break; // No more data to send
                    }
                    
                    MPI_Isend(buff, coreset_size * features, MPI_FLOAT, i, 0, MPI_COMM_WORLD, &request[i - 1]);
                    sent++;
                    local_count++;
                }

                // Wait for all sends to complete
                MPI_Waitall(local_count, request, MPI_STATUSES_IGNORE);
            }
        } else if (master_type == "async2") {
            MPI_Request request[world_size - 1];
            int indices[world_size - 1];
            int sent = 0;
            int completed = MPI_UNDEFINED;

            for (int i = 1; i < world_size; ++i) {
                MPI_Isend(buff, coreset_size * features, MPI_FLOAT, i, 0, MPI_COMM_WORLD, &request[i - 1]);
            }

            sent = world_size - 1; 

            while (true) {
                MPI_Waitsome(world_size - 1, request, &completed, indices, MPI_STATUSES_IGNORE);
                if (completed == MPI_UNDEFINED) {
                    break; // No more requests to complete
                }

                for (int i = 0; i < completed; ++i) {
                    if (sent < stream_size) {
                        int rank = indices[i] + 1; // Adjust for rank starting from 1
                        MPI_Isend(buff, coreset_size * features, MPI_FLOAT, rank, 0, MPI_COMM_WORLD, &request[indices[i]]);
                        sent++;
                    } 
                }
            }

        } else if (master_type == "sync") {
            int sent = 0;
            int next_rank = 0;
            while (sent < stream_size) {
                MPI_Send(buff, coreset_size * features, MPI_FLOAT, next_rank + 1, 0, MPI_COMM_WORLD);
                sent++;
                next_rank = (next_rank + 1) % (world_size - 1); // Cycle through ranks 1 to world_size - 1
            }
        } else if (master_type == "pull-sync") {
            int sent = 0;
            MPI_Request request[world_size - 1];
            int indices[world_size - 1];

            for (int i = 1; i < world_size; ++i) {
                MPI_Irecv(nullptr, 0, MPI_FLOAT, i, 0, MPI_COMM_WORLD, &request[i - 1]); 
            }

            while (sent < stream_size) {
                int completed;
                MPI_Waitsome(world_size - 1, request, &completed, indices, MPI_STATUSES_IGNORE);
                for (int i = 0; i < completed; ++i) {
                    int rank = indices[i] + 1; // Adjust for rank starting from 1
                    MPI_Send(buff, coreset_size * features, MPI_FLOAT, rank, 0, MPI_COMM_WORLD);
                    MPI_Irecv(nullptr, 0, MPI_FLOAT, rank, 0, MPI_COMM_WORLD, &request[indices[i]]);
                    sent++;
                }
            }
        }  else if (master_type == "pull-async") {
            int sent = 0;
            MPI_Request request[world_size - 1];
            int indices[world_size - 1];
            bool is_recv[world_size - 1];

            for (int i = 1; i < world_size; ++i) {
                MPI_Irecv(nullptr, 0, MPI_FLOAT, i, 0, MPI_COMM_WORLD, &request[i - 1]); 
                is_recv[i - 1] = true;
                sent++;
            }

            while (sent < stream_size) {
                int completed;
                MPI_Waitsome(world_size - 1, request, &completed, indices, MPI_STATUSES_IGNORE);

                for (int i = 0; i < completed; ++i) {
                    int rank = indices[i] + 1; // Adjust for rank starting from 1
                    
                    if (is_recv[indices[i]]) { //send 
                        MPI_Isend(buff, coreset_size * features, MPI_FLOAT, rank, 0, MPI_COMM_WORLD, &request[indices[i]]);
                        is_recv[indices[i]] = false; // Mark as sent
                    } else { // receive
                        if (sent >= stream_size) {
                            continue; // No more data to send
                        }

                        MPI_Irecv(nullptr, 0, MPI_FLOAT, rank, 0, MPI_COMM_WORLD, &request[indices[i]]);
                        is_recv[indices[i]] = true; // Mark as received
                        sent++;
                    }
                }
            }
        }
        
        else {
            std::cerr << "Unknown master type: " << master_type << std::endl;
            MPI_Abort(MPI_COMM_WORLD, 1);
        }

        // sync send termination signal
        for (int i = 1; i < world_size; ++i) {
            MPI_Send(buff, coreset_size * features, MPI_FLOAT, i, 1, MPI_COMM_WORLD); // Tag 1 for exit signal
        }

    } else { 

        // create a generator for random work time 
        std::default_random_engine generator;
        std::normal_distribution<double> distribution(work_time, work_std_dev);



        size_t coreset_size, features;
        MPI_Bcast(&coreset_size, 1, MPI_UINT64_T, 0, MPI_COMM_WORLD);
        MPI_Bcast(&features, 1, MPI_UINT64_T, 0, MPI_COMM_WORLD);

        // std::cout << "Process " << world_rank << " received coreset size: " << coreset_size 
        //           << " and features: " << features << std::endl;

        float* buff = new float[coreset_size * features];
        
        start = std::chrono::high_resolution_clock::now();
        MPI_Status status;

        size_t samples_count = 0;

        while(true) {
            if (master_type == "pull-sync" || master_type == "pull-async") {
                MPI_Send(nullptr, 0, MPI_FLOAT, 0, 0, MPI_COMM_WORLD); // Empty send to trigger pull
            } 

            MPI_Recv(buff, coreset_size * features, MPI_FLOAT, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
            int tag = status.MPI_TAG;

            if (tag == 0) {
                samples_count++;

                if (samples_count % 2 == 0) {
                    double work_duration = distribution(generator);
                 
                    std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(work_duration)));
                }

            } else if (tag == 1) {
                // Exit signal
                // std::cout << "Process " << world_rank << " received exit signal." << std::endl;
                break;
            }

        }

    }

    auto end = std::chrono::high_resolution_clock::now();
    auto delta = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    auto delta_sum = 0;
    MPI_Reduce(&delta, &delta_sum, 1, MPI_UINT64_T, MPI_SUM, 0, MPI_COMM_WORLD);
    if (world_rank == 0) {
        std::cout << "Total time taken: " << delta_sum / 1000000.0 << " seconds." << std::endl;
        std::cout << "Average time per process: " << delta_sum / (1000000.0 * world_size) << " seconds." << std::endl;
    }

    MPI_Finalize();
    return 0;
}