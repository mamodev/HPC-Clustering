#include <mpi.h>
#include <iostream>
#include <chrono>   

#include "parser.hpp"
#include "coreset_stream.hpp"


class MPIStream : public CoresetStream {
private:
     MPI_Status status;
     MPI_Request request;
     std::vector<float> buff;
     int source_rank;
public:
     MPIStream(size_t coreset_size, size_t feature, int source_rank) : CoresetStream() {
          this->coreset_size = coreset_size;
          this->features = feature;
          this->processed_batches = 0;
          this->source_rank = source_rank;
          this->buff.resize((coreset_size * 2) * feature);
          MPI_Irecv(buff.data(), (coreset_size * 2) * feature, MPI_FLOAT, source_rank, 0, MPI_COMM_WORLD, &request);
     }

     
     std::vector<float> next_batch() override {
          processed_batches++;

          std::vector<float> new_buff((coreset_size * 2) * features);

          int count;     
          MPI_Wait(&request, &status);
          MPI_Get_count(&status, MPI_FLOAT, &count);
          if (count == 0) {
               return std::vector<float>();
          }
          this->buff.resize(count);

          MPI_Irecv(new_buff.data(), (coreset_size * 2) * features, MPI_FLOAT, source_rank, 0, MPI_COMM_WORLD, &request);

          std::swap(buff, new_buff);

          return std::move(new_buff);
     }

};


int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    int world_size, world_rank;
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);

    if (world_size < 2) {
        std::cerr << "This application requires at least two processes." << std::endl;
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
    }

    if (world_rank == 0) {

          MemoryStream stream(argc, argv);

          MPI_Bcast(&stream.coreset_size, 1, MPI_UNSIGNED_LONG, 0, MPI_COMM_WORLD);
          MPI_Bcast(&stream.features, 1, MPI_UNSIGNED_LONG, 0, MPI_COMM_WORLD);


          auto start = std::chrono::high_resolution_clock::now();
          int next_rank = 0;
          while(true) {
               auto batch = stream.next_batch();
               if (batch.empty()) {
                    break;
               }

               MPI_Send(batch.data(), batch.size(), MPI_FLOAT, next_rank + 1, 0, MPI_COMM_WORLD);
               next_rank = (next_rank + 1) % (world_size - 1); // Send to the next rank, skipping rank 0
          }

          for (int i = 1; i < world_size; ++i) {
               MPI_Send(nullptr, 0, MPI_FLOAT, i, 0, MPI_COMM_WORLD); // Send an empty batch to signal completion
          }
          auto end = std::chrono::high_resolution_clock::now();

          std::chrono::duration<double> elapsed = end - start;
          double local_elapsed = elapsed.count();
          double max_time;
          MPI_Reduce(&local_elapsed, &max_time, 1, MPI_DOUBLE, MPI_MAX, 0,
               MPI_COMM_WORLD);

          std::cout << "Maximum elapsed time: " << max_time << " seconds" << std::endl;

    } else {

          size_t coreset_size, features;
          MPI_Bcast(&coreset_size, 1, MPI_UNSIGNED_LONG, 0, MPI_COMM_WORLD);
          MPI_Bcast(&features, 1, MPI_UNSIGNED_LONG, 0, MPI_COMM_WORLD);

          // std::cout << "Rank " << world_rank << " initialized with coreset_size: " << coreset_size << " and features: " << features << std::endl;
          
          auto start = std::chrono::high_resolution_clock::now();
          
          MPIStream stream(coreset_size, features, 0);
          auto coreset = coresetStreamOmp<int32_t, 3, 3U>(stream);
          
          auto end = std::chrono::high_resolution_clock::now();
          std::chrono::duration<double> elapsed = end - start;
          double local_elapsed = elapsed.count(); // Store the value
      
          // Contribute to the reduction for the maximum time
          MPI_Reduce(&local_elapsed, nullptr, 1, MPI_DOUBLE, MPI_MAX, 0,
                     MPI_COMM_WORLD);
    }

    MPI_Finalize();
    return 0;
}