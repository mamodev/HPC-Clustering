#include <mpi.h>
#include <vector>
#include <cstring>

#include "assert.hpp"

#include <set>


#if !defined(CORESET_SIZE)
#define CORESET_SIZE 600
#endif

// constexpr int TASK_REQ_TAG = -1; 

// class Worker {

// private:
//     static constexpr int MASTER_RANK = 0;
//     static constexpr size_t MAX_IN_PENDING_REQ = 10;
//     static constexpr size_t MAX_OUT_PENDING_REQ = 10;

//     int features = -1;
//     int batch_size = -1;

//     int rank = -1;
//     int world_size = -1;
//     MPI_Comm comm;



//     enum class ReqType {
//         SEND,
//         RECV,
//     };

//     struct req_info {
//         ReqType type;
//         float *buff;
//     };

//     std::vector<int> mpi_indices;   
//     std::vector<MPI_Request> mpi_requests;
//     std::vector<MPI_Status> mpi_statuses;
//     std::vector<req_info> req_infos;

//     void assert_initialization() {
//         assert(rank >= 0, "Worker rank must be non-negative.");
//         assert(world_size > 0, "World size must be greater than zero.");
//         assert(comm != MPI_COMM_NULL, "MPI communicator must be initialized.");
//         assert(features > 0, "Features must be greater than zero.");
//         assert(batch_size > 0, "Batch size must be greater than zero.");

//         static_assert(MAX_IN_PENDING_REQ > 0, "MAX_IN_PENDING_REQ must be greater than zero.");
//         static_assert(MAX_OUT_PENDING_REQ > 0, "MAX_OUT_PENDING_REQ must be greater than zero.");
//         static_assert(MASTER_RANK >= 0, "MASTER_RANK must be non-negative.");
//         assert(MASTER_RANK < world_size, "MASTER_RANK must be less than world_size.");
//     }

// public:

//     Worker(int rank, int world_size, MPI_Comm comm, int features, int batch_size)
//         : rank(rank), world_size(world_size), comm(comm), features(features), batch_size(batch_size) {}


//     void loop() {
//         assert_initialization();
        
//         mpi_statuses.resize(MAX_IN_PENDING_REQ + MAX_OUT_PENDING_REQ);
//         mpi_requests.resize(MAX_IN_PENDING_REQ + MAX_OUT_PENDING_REQ);
//         mpi_indices.resize(MAX_IN_PENDING_REQ + MAX_OUT_PENDING_REQ);
//         req_infos.resize(MAX_IN_PENDING_REQ + MAX_OUT_PENDING_REQ);

//         for (size_t i = 0; i < MAX_IN_PENDING_REQ; ++i) {
//             float *buff = new float[batch_size * (features + 1)];
//             MPI_Irecv(&buff, sizeof(float) * batch_size * (features + 1)
//                 , MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, comm, &mpi_requests[i]);

//             req_infos[i].type = ReqType::RECV;
//             req_infos[i].buff = buff;
//         }

//         for (size_t i = MAX_IN_PENDING_REQ; i < MAX_IN_PENDING_REQ + MAX_OUT_PENDING_REQ; ++i) {
//             MPI_Isend(nullptr, 0, MPI_FLOAT, MASTER_RANK, TASK_REQ_TAG, comm, &mpi_requests[i]);
//             req_infos[i].type = ReqType::SEND;
//             req_infos[i].buff = nullptr;
//         }

//         int completed = 0;
//         while (true) {
//             MPI_Waitsome(mpi_requests.size(), mpi_requests.data(), &completed, mpi_indices.data(), mpi_statuses.data());
//             if (completed > 0) {
//                 for (int i = 0; i < completed; ++i) {
//                     auto& info = req_infos[mpi_indices[i]];
//                     if (info.type == ReqType::SEND) {
//                         if (info.buff) {
//                             delete[] info.buff;
//                             info.buff = nullptr;
//                         }

//                         continue;
//                     }

//                     // Handle RECV
//                     // Repost the receive request
//                     MPI_Irecv(info.buff, CORESET_SIZE, MPI_FLOAT, MPI_ANY_SOURCE, MPI_ANY_TAG, comm, &mpi_requests[mpi_indices[i]]);
//                 }
//             }
//         }
//     }

// };




int main(int argc, char **argv)
{
    MPI_Init(&argc, &argv);

    int world_rank, world_size;
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
    
    assert(world_size > 2, "World size must be greater than 2.");


    int features = 3;


    if (world_rank == 0) {

        bool wready[world_size];
        memset(wready, 0, sizeof(bool) * (world_size));

        std::set<uint32_t> wset[world_size];
        for (int i = 0; i < world_size; ++i)
            wset[i].clear();
       
        
        std::vector<MPI_Request> reqs;
        std::vector<MPI_Status> stats;
        std::vector<int> indices;
        std::vector<bool> is_recv;

        for (int i = 1; i < world_size; ++i) {
            MPI_Irecv(nullptr, 0, MPI_BYTE, MPI_ANY_SOURCE, -1, MPI_COMM_WORLD, &reqs.emplace_back());
            is_recv.push_back(true);
        }


        float *chunk = new float[CORESET_SIZE * (features)];

        int completed = 0;
        while (true) {
            MPI_Waitsome(reqs.size(), reqs.data(), &completed, indices.data(), stats.data());
            if (completed > 0) {
                for (int i = 0; i < completed; ++i) {
                    int idx = indices[i];
                    if (is_recv[idx]) {
                        int source = stats[i].MPI_SOURCE;
                        wready[source] = true;
                        is_recv[idx] = false;
                        indices[i] = -1; 
                        MPI_Irecv(nullptr, 0, MPI_BYTE, MPI_ANY_SOURCE, -1, MPI_COMM_WORLD, &reqs[idx]);
                    }
                }
            }
            
            // remove all completed requests that are not -1 
            reqs.erase(std::remove_if(reqs.begin(), reqs.end(), [](const MPI_Request& req) {
                return req == MPI_REQUEST_NULL;
            }), reqs.end());

            is_recv.erase(std::remove_if(is_recv.begin(), is_recv.end(), [](bool recv) {
                return !recv;
            }), is_recv.end());

            indices.resize(reqs.size());
            stats.resize(reqs.size());


            for (int i = 1; i < world_size; ++i) {
                if(!wready[i]) {
                    continue;
                }


           
            }

        }
        
    } else {
    
    }


    MPI_Finalize();
}