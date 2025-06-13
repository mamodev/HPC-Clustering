#include "CoresetTree.hpp"
#include "perf.hpp"
#include "mpi_wrap.h"
#include "parser.hpp"
#include <set>
#include <optional>
#include <chrono>
#include <ranges>

// #undef _cout
// #define _cout std::cout

#if !defined(CORESET_SIZE)
#define CORESET_SIZE 10000
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


constexpr size_t MAX_RANK = 100; // Maximum rank for the rank map

class NodeState {
    std::set<int> ranks;

    std::vector<int> rank_masters; // Master for each rank
    std::vector<int> rank_lowest_priority_nodes; 
    std::vector<std::vector<int>> inputs;
    std::vector<std::vector<int>> outputs;
    std::vector<std::pair<std::vector<int>, std::vector<int>>> siblings; // [Lower priority siblings, Higher priority siblings]
    std::vector<std::vector<int>> terminations;

public:
    int node_id;
    constexpr static uint32_t END_OF_PROGRAM = std::numeric_limits<uint32_t>::max(); // Special value to indicate end of program

    NodeState(const std::vector<std::vector<int>>& rank_map, int node) {
        assert(rank_map.size() > 0 && "Rank map must not be empty");

        node_id = node;
        inputs.resize(rank_map.size());
        outputs.resize(rank_map.size());
        siblings.resize(rank_map.size());
        terminations.resize(rank_map.size());
        
        // for (auto [r, workers] : rank_map | std::views::enumerate) {
        for (size_t r = 0; r < rank_map.size(); ++r) {
            const auto& workers = rank_map[r];
            rank_masters.push_back(workers.front()); // The first worker in the rank is the master
            rank_lowest_priority_nodes.push_back(workers.back()); // The last worker in the rank is the lowest priority node
        
            if(std::find(workers.begin(), workers.end(), node) != workers.end()) 
                ranks.insert(r); 
        }

        

        assert(!ranks.empty() && "Node must have at least one rank associated with it");

        // for (auto [r, workers] : rank_map | std::views::enumerate) {
        for (size_t r = 0; r < rank_map.size(); ++r) {
            const auto& workers = rank_map[r];
            bool found_current = false;
            bool has_next_rank_as_input = ranks.contains(r + 1);
            bool has_this_rank_as_input = ranks.contains(r);
            bool has_this_rank_as_output = ranks.contains(r - 1);

            if (!has_this_rank_as_input && !has_this_rank_as_output && !has_next_rank_as_input) 
                continue;

            for (auto w : workers | std::views::reverse) { // Reverse to prioritize higher priority workers
                if (w == node) {
                    found_current = true;
                    continue;
                }

                if (has_this_rank_as_input) {
                    // siblings[r] are all worker that takes input from this rank
                    // so that are present in rank_map[r]
                    if (!found_current) {
                        siblings[r].first.push_back(w); // Lower priority siblings
                    } else {
                        siblings[r].second.push_back(w); // Higher priority siblings
                    }
                }

                if (has_next_rank_as_input) {
                    inputs[r + 1].push_back(w);
                }

                if (has_this_rank_as_input) {
                    outputs[r].push_back(w);
                }
            }
        }

        // Add master as input for rank 0   
        if (ranks.contains(0)) {
            inputs[0].push_back(0); // Master node is always rank 0
        }

        assert(inputs[0].size() <= 1 && "Rank 0 should have at most one input (the master node itself)");
        assert(inputs[0].empty() || inputs[0][0] == 0 && "Rank 0 should have at most one input (the master node itself)");
    }

    bool has_received_master_termination() const {
        return std::find(terminations[0].begin(), terminations[0].end(), 0) != terminations[0].end();
    }

    bool __check_valid_rank(int rank) const {
        return ranks.contains(rank) || (is_lowest_priority_sibling(rank + 1));
    }

    bool __check__worker_is_input(int rank, int worker) const {
        return std::find(inputs[rank].begin(), inputs[rank].end(), worker) != inputs[rank].end();
    }

    bool __check_woker_is_lower_priority_sibling(int rank, int worker) const {
        return std::find(siblings[rank].first.begin(), siblings[rank].first.end(), worker) != siblings[rank].first.end();
    }

    bool __check_worker_is_master_of_rank(int rank, int worker) const {
        return rank_masters[rank] == worker; // For other ranks, check if the worker is the master of that rank
    }

    void add_termination(int rank, int worker) {

        assert(__check_valid_rank(rank) && "Rank must be part of the node's ranks or a lower priority sibling");


        assert(__check__worker_is_input(rank, worker) || __check_woker_is_lower_priority_sibling(rank, worker) || __check_worker_is_master_of_rank(rank, worker) &&
               "Worker must be part of the node's inputs or lower priority siblings or master for the given rank");
        assert(std::find(terminations[rank].begin(), terminations[rank].end(), worker) == terminations[rank].end() && 
               "Worker must not already be in the node's inputs terminations for the given rank");

        terminations[rank].push_back(worker);
    }
    
    // bool all_inputs_terminated(int rank) const {
    //     assert(ranks.contains(rank) && "Rank must be part of the node's ranks");

    //     for (const auto& worker : inputs[rank]) {
    //         if (std::find(terminations[rank].begin(), terminations[rank].end(), worker) == terminations[rank].end()) {
    //             return false; // If any input worker has not terminated, return false
    //         }
    //     }

    //     return true; // All input workers have terminated for the given rank
    // }

    bool input_master_terminated(int rank) const {
        assert(__check_valid_rank(rank) && "Rank must be part of the node's ranks or a lower priority sibling");

        if (rank == 0) {
            return has_received_master_termination(); // Special case for rank 0, check if master has terminated
        }

        // For other ranks, check if the master worker (rank_masters[rank]) has terminated
        int master_worker = rank_masters[rank - 1];
        
        if (master_worker == node_id) {
            return true; // If the master worker is the current node, it cannot be terminated
        }

        return std::find(terminations[rank - 1].begin(), terminations[rank - 1 ].end(), master_worker) != terminations[rank - 1].end();
    }

    bool in_comunication_closed(int rank) const {
        
        if (is_lowest_priority_sibling(rank)) {
            return input_master_terminated(rank); 
        }

        if (rank == 0) {
            return  has_received_master_termination() && lower_priority_sibling_terminated(rank);
        }

        return lower_priority_sibling_terminated(rank); 
    }

    bool all_in_comunication_closed() const {
        for (int r : ranks) {
            if (!in_comunication_closed(r)) 
                return false; 
        }

        return true; 
    }

    bool has_no_outputs(int rank) const {
        assert(ranks.contains(rank) && "Rank must be part of the node's ranks");

        return outputs[rank].empty(); // For other ranks, check if there are no outputs
    }

    bool lower_priority_sibling_terminated(int rank) const {
        assert(siblings.size() > rank && "Siblings must have at least as many ranks as the node's ranks");

        if (siblings[rank].first.size() == 0) {
            return true; // No lower priority siblings, so they are considered terminated
        }


        int prev_sibling = siblings[rank].first[siblings[rank].first.size() - 1];
        return std::find(terminations[rank].begin(), terminations[rank].end(), prev_sibling) != terminations[rank].end();
    }

    bool is_lowest_priority_sibling(int rank) const {
        if(!ranks.contains(rank)) {
            return false;
        }

        if (siblings[rank].first.empty()) {
            return true; // No lower priority siblings, so this is the lowest priority sibling
        }

        return std::find(siblings[rank].first.begin(), siblings[rank].first.end(), node_id) != siblings[rank].first.end();
    }

    std::pair<uint32_t, int> where_to_flush(int signaling_worker, int rank) const { // Returns a pair of (worker ID, rank to flush)
        assert(__check_valid_rank(rank) && "Rank must be part of the node's ranks or a lower priority sibling");
        assert(lower_priority_sibling_terminated(rank) && "All lower priority siblings must be terminated for the given rank");

        // is signaling_worker the master of the rank? if it is this is not a normal flush
        if (rank_masters[rank] == signaling_worker) {
            return where_to_flush(-1, rank + 1); // Flush to the master of the rank
        }


        if (!siblings[rank].second.empty()) {
            int sib = siblings[rank].second.front();
            // _cout << "P[" << node_id << "] flushing to higher priority sibling for rank " << rank << std::endl;
            return {sib, rank}; // Return the first higher priority sibling and the current rank
        }

        // else we are the HIGHEST priority sibling for this rank
        // so what we can do is flush to the next rank LOWEST priority sibling
        if (rank == rank_lowest_priority_nodes.size() - 1) {
            // _cout << "P[" << node_id << "] flushing to END_OF_PROGRAM for rank " << rank << std::endl;
            // return END_OF_PROGRAM; // Special value to indicate that this is the last rank and we can exit
            return {END_OF_PROGRAM, rank}; // Return END_OF_PROGRAM and the current rank
        }

        // _cout << "P[" << node_id << "] flushing to next lowest priority node for rank " << rank << " so next rank is " << rank + 1 << std::endl;
        // return rank_lowest_priority_nodes[rank + 1]; // Return the master of the next rank
        return {rank_lowest_priority_nodes[rank + 1], rank }; // Return the master of the next rank and the current rank
    }

    bool has_first_class_rank_support(int rank) const {
        return ranks.contains(rank);
    }

    bool is_rank_master(int rank, int worker) const {
        assert(rank >= 0 && rank < rank_masters.size() && "Rank must be within the bounds of rank_masters");

        if (rank == 0) {
            return worker == 0; // For rank 0, the master is always worker 0
        }
        return rank_masters[rank] == worker; // For other ranks, check if the worker is the master of that rank
    }
};


void init_merge_buffer(
    float *merge_buffer,
    float *coreset1,
    float *coreset2,
    bool has_weights1,
    bool has_weights2,
    size_t coreset_size,
    size_t features
) {
    assert(merge_buffer != nullptr && "Merge buffer must not be null");
    assert(coreset1 != nullptr && "Coreset 1 must not be null");
    assert(coreset2 != nullptr && "Coreset 2 must not be null");
    assert(coreset_size > 0 && "Coreset size must be greater than 0");
    assert(features > 0 && "Features must be greater than 0");

    size_t offset = 0;

    // Copy coreset1 and coreset2 (interleaving them)
    for (size_t i = 0; i < coreset_size; ++i) {
        for (size_t j = 0; j < features; ++j) {
            merge_buffer[offset++] = coreset1[i * features + j]; // Copy from coreset1
        }

        for (size_t j = 0; j < features; ++j) {
            merge_buffer[offset++] = coreset2[i * features + j]; // Copy from coreset2
        }
    }   

    if (!has_weights1 && !has_weights2) {
        // If neither coreset has weights, we can return early
        return;
    }

    for (size_t i = 0; i < coreset_size; ++i) {
        if (has_weights1) {
            merge_buffer[offset++] = coreset1[coreset_size * features + i]; // Copy weight from coreset1
        } else {
            merge_buffer[offset++] = 0.0f; // No weight for coreset1
        }

        if (has_weights2) {
            merge_buffer[offset++] = coreset2[coreset_size * features + i]; // Copy weight from coreset2
        } else {
            merge_buffer[offset++] = 0.0f; // No weight for coreset2
        }
    }
}


std::vector<std::vector<int>> genRankMap(int world_size, int max_rank)
{   
    assert(world_size > 0 && "Number of workers must be greater than 0");
    assert(max_rank > 0 && "Maximum rank must be greater than 0");
    assert(world_size % 2 != 0 && "Number of workers must be even: (world_size - 1) is the number of workers excluding the master node");

    std::vector<std::vector<int>> rank_map(max_rank);

    // int next_worker = 0;
    int num_worker_per_rank = world_size - 1; // Exclude the master node
    int last_split_rank = -1;
    for (int r = 0; r < max_rank; ++r) {
        for (int i = 0; i < num_worker_per_rank; ++i) {
            // rank_map[r].push_back(next_worker + 1); // +1 to skip the master node
            // next_worker = (next_worker + 1) % (world_size - 1); // Wrap around to the next worker, excluding the master node
            rank_map[r].push_back(i + 1); // +1 to skip the master node
        
        }

        num_worker_per_rank = (num_worker_per_rank) / 2; // Halve the number of workers for the next rank
        if (num_worker_per_rank == 1) {
            last_split_rank = r; // Remember the last rank that had workers
            break;
        }
    }

    assert(last_split_rank != -1 && "There should be at least one rank with workers");
    assert(!rank_map.empty() && "Rank map should not be empty");

    // assign rank tail to Woker1 
    for (int r = last_split_rank + 1; r < max_rank; ++r) {
        rank_map[r].push_back(1); // Assign Worker 1 to all remaining ranks
    }
    
    return rank_map;
}

// std::vector<std::vector<int>> genRankMap(int world_size, int max_rank)
// {   
//     assert(world_size > 0 && "Number of workers must be greater than 0");
//     assert(max_rank > 0 && "Maximum rank must be greater than 0");
//     assert(world_size % 2 != 0 && "Number of workers must be even: (world_size - 1) is the number of workers excluding the master node");

//     std::vector<std::vector<int>> rank_map(max_rank);

//     int next_worker = 0;
//     int num_worker_per_rank = world_size - 1; // Exclude the master node
//     int last_split_rank = -1;
//     for (int r = 0; r < max_rank; ++r) {
//         for (int i = 0; i < num_worker_per_rank; ++i) {
//             rank_map[r].push_back(next_worker + 1); // +1 to skip the master node
//             next_worker = (next_worker + 1) % (world_size - 1); // Wrap around to the next worker, excluding the master node
//             // rank_map[r].push_back(i + 1); // +1 to skip the master node
        
//         }

//         num_worker_per_rank = (num_worker_per_rank) / 2 + (num_worker_per_rank % 2 != 0 ? 1: 0);



//         if (num_worker_per_rank == 1) {
//             last_split_rank = r; // Remember the last rank that had workers
//             break;
//         }
//     }

//     assert(last_split_rank != -1 && "There should be at least one rank with workers");
//     assert(!rank_map.empty() && "Rank map should not be empty");

//     // assign rank tail to Woker1 
//     for (int r = last_split_rank + 1; r < max_rank; ++r) {
//         // // rank_map[r].push_back(1); // Assign Worker 1 to all remaining ranks
//         // next_worker = (next_worker + 1) % (world_size - 1);
//         rank_map[r].push_back(next_worker + 1); // +1 to skip the master node
//     }   
    
//     return rank_map;
// }

int randomRankWorker(int rank, const std::vector<std::vector<int>>& rank_map)
{
    assert(rank >= 0 && rank < rank_map.size() && "Rank out of bounds");
    const auto& workers = rank_map[rank];
    assert(!workers.empty() && "No workers available for this rank");
    
    return workers[rand() % workers.size()];
}

template <typename T>
void remove_indices_from_vector(
    const std::span<const int>& indices, 
    std::vector<T>& vec
) {
    std::vector<bool> to_remove(vec.size(), false);
    
    for (int idx : indices) {
        if (idx >= 0 && static_cast<size_t>(idx) < vec.size()) {
            to_remove[idx] = true;
        }
    }
    
    size_t write = 0;
    for (size_t read = 0; read < vec.size(); ++read) {
        if (!to_remove[read]) {
            if (write != read) {
                vec[write] = std::move(vec[read]);
            }
            ++write;
        }
    }
    vec.resize(write);
}

template <typename... Vecs>
void remove_indices(const std::span<const int>& indices, Vecs&... vecs) {
    (remove_indices_from_vector(indices, vecs), ...);
}

int main(int argc, char **argv)
{
    auto perf = PerfManager();
    perf.pause(); // Pause perf at the start

    MPI_Init(&argc, &argv);

    int world_rank, world_size;
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);


    if ((world_size - 1) % 2 != 0) {
        std::cerr << "Number of workers must be even (world_size - 1)! Current world size: " << world_size << std::endl;
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
    }

    std::cout << "RANK " << world_rank << " PID: " << getpid() << std::endl;
    MPI_Barrier(MPI_COMM_WORLD); // Ensure all ranks are synchronized before proceeding


    // std::string dbg = "perl -w pgdb/client.pl " + std::string(argv[0]) + " " + std::to_string(getpid()) + " " + std::to_string(world_rank) + " &";
    // system(dbg.c_str()); // This is just for debugging purposes, to see the process IDs and ranks
    // sleep(4); // Sleep for a second to allow the perl script to start

    constexpr int MASTER_RANK = 0; // Master rank is always 0

    std::vector<std::vector<int>> rank_map = genRankMap(world_size, MAX_RANK);

    MPI_Comm worker_comm;
    MPI_Comm_split(MPI_COMM_WORLD, world_rank == MASTER_RANK ? MPI_UNDEFINED : 1, world_rank, &worker_comm); // Split the communicator into master and workers

    if (world_rank == MASTER_RANK) {
        _cout << "Master process started." << std::endl;

        // print rank map
        std::cout << "Rank map: " << std::endl;
        bool found_one = false;
        for (size_t r = 0; r < rank_map.size(); ++r) {
            if (rank_map[r].size() == 1) {
                if (found_one) break;
                found_one = true;
            }

            std::cout << "Rank " << r << ": ";
            for (const auto& worker : rank_map[r]) {
                std::cout << worker << " ";
            }
            std::cout << std::endl;
        }

        auto start_time = std::chrono::high_resolution_clock::now();
        auto [samples, outPath, _] = parseArgs<float>(argc, argv);
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        std::cout << "Parsing took " << duration.count() << " ms" << std::endl;


        for (size_t r = 1; r < world_size; ++r) {
            mpi_send(&samples.features, 1, MPI_UNSIGNED_LONG, r, 0, MPI_COMM_WORLD);
        }

        size_t num_workers = world_size - 1;
        size_t next_worker = 0;


        std::vector<MPI_Request> requests;
        std::vector<MPI_Status> statuses; // Statuses for the requests
        std::vector<int> request_indices; // Indices of completed requests
        
        perf.resume(); // Resume perf after parsing

        size_t stream_cursor = 0;
        size_t stream_size = samples.samples; 

        auto get_chunk_at = [&](size_t cursor) -> const float* {
            size_t wrapped_cursor = cursor % samples.samples;
            if (wrapped_cursor + CORESET_SIZE > samples.samples) {
                wrapped_cursor = samples.samples - CORESET_SIZE; // Ensure we don't go out of bounds
            }

            return samples.data.data() + wrapped_cursor * samples.features; // Get the chunk of data at the cursor
        };


        for (int w = 0; w < num_workers; ++w) {
            const float *chunk = get_chunk_at(stream_cursor);
            stream_cursor += CORESET_SIZE;
            mpi_isend(chunk, CORESET_SIZE * samples.features, MPI_FLOAT, w + 1, 0, MPI_COMM_WORLD, &requests.emplace_back());
        }

        statuses.resize(requests.size());
        request_indices.resize(requests.size());

        std::set<int> eof_idx;

        size_t STAT_CORESET_SENT = num_workers; // Number of coresets sent by rank 0
        while (true) {        
            int outcount = 0;
            mpi_waitsome(requests.size(), requests.data(), &outcount, request_indices.data(), statuses.data());

            if (outcount == MPI_UNDEFINED) {
                break; // All messages sent, exit the loop
            }

            assert(outcount > 0 && "mpi_waitsome returned negative outcount or zero, which should not happen here");
            for (int r = 0; r < outcount; ++r) {
                int idx = request_indices[r];
                if (eof_idx.contains(idx)) {
                    continue;
                }

                if (stream_cursor + CORESET_SIZE > stream_size) {
                    eof_idx.insert(idx);
                    mpi_isend(nullptr, 0, MPI_FLOAT, idx + 1, 0, MPI_COMM_WORLD, &requests[idx]);
                } else {
                    const float *chunk = get_chunk_at(stream_cursor);
                    stream_cursor += CORESET_SIZE;
                    mpi_isend(chunk, CORESET_SIZE * samples.features, MPI_FLOAT, idx + 1, 0, MPI_COMM_WORLD, &requests[idx]);
                    STAT_CORESET_SENT += 1;
                }
            }
        }

        std::cout << "Master process finished sending data to workers. total rank0 coresets sent: " << STAT_CORESET_SENT << std::endl;
    } else {


        size_t STAT_AGGS = 0; // Number of aggregations performed
        size_t STAT_RANK0_CORESETS = 0; // Number of rank 0 coresets received
        size_t STAT_RANK0_AGGS = 0; // Number of rank 0 aggregations performed
        size_t STAT_RANK1_AGGS = 0; // Number of rank 1 aggregations performed

        // _cout << "P[" << world_rank << "] started." << std::endl;
        NodeState node_state(rank_map, world_rank);

        size_t features;
        MPI_Recv(&features, 1, MPI_UNSIGNED_LONG, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        _cout << "P[" << world_rank << "] received features: " << features << std::endl;

        std::vector<float*> coresets(MAX_RANK, nullptr); 
        const size_t CORESET_POINTS_SIZE = CORESET_SIZE * features; 
        const size_t CORESET_WEIGHTS_SIZE = CORESET_SIZE; 
        const size_t CORESET_TOTAL_SIZE = CORESET_POINTS_SIZE + CORESET_WEIGHTS_SIZE; 

        struct request_info_t {
            bool is_send_req = false; // True if this is a send request, false if it is a receive request
            float *buffer = nullptr; 
        };

        std::vector<MPI_Request> master_requests; 
        std::vector<MPI_Request> peer_requests; 

        std::vector<MPI_Status> master_request_statuses;
        std::vector<MPI_Status> peer_request_statuses;
        
        std::vector<int> peer_requests_indices; 
        std::vector<int> master_requests_indices;

        std::vector<request_info_t> master_request_info;
        std::vector<request_info_t> peer_request_info;

        auto issue_master_req = [&]() {
            float *recv_buff = new float[CORESET_POINTS_SIZE];
            mpi_irecv(recv_buff, CORESET_POINTS_SIZE, MPI_FLOAT, MASTER_RANK, 0, MPI_COMM_WORLD, &master_requests.emplace_back());
            master_request_info.emplace_back(request_info_t{false, recv_buff});
        };

        bool issue_new_peer_request = true;
        bool event_clear_peers_recvs = false; 
        bool event_clear_master_recvs = false;

        auto issue_peers_req = [&]() {
            float *recv_buff = new float[CORESET_TOTAL_SIZE];
            mpi_irecv(recv_buff, CORESET_TOTAL_SIZE, MPI_FLOAT, MPI_ANY_SOURCE, MPI_ANY_TAG, worker_comm, &peer_requests.emplace_back());
            peer_request_info.emplace_back(request_info_t{false, recv_buff});
        };
        
        auto issue_send_peer_req = [&](int peer, int rank, float *coreset_buff, size_t buff_size) {
            mpi_isend(coreset_buff, buff_size, MPI_FLOAT, peer - 1, rank, worker_comm, &peer_requests.emplace_back());
            peer_request_info.emplace_back(request_info_t{true, coreset_buff});
        };  


        float *merge_buffer = new float[CORESET_TOTAL_SIZE * 2]; 
        float *coreset_extract_buffer = new float[CORESET_TOTAL_SIZE]; 

        auto handle_recv = [&](bool is_master_msg,  request_info_t info, const MPI_Status& status) {
            int cancelled = 0;
            MPI_Test_cancelled(&status, &cancelled);
            if (cancelled || info.is_send_req) {

                // std::cout << "P[" << world_rank << "] Ignoring cancelled send request from worker " 
                //           << (is_master_msg ? "master" : "peer") 
                //           << " with rank " << status.MPI_TAG 
                //           << " (source: " << status.MPI_SOURCE << ")" 
                //           << std::endl;

                if (info.buffer != nullptr) 
                    delete[] info.buffer; 
                return;
            }

            // std::string type = info.is_send_req ? "send" : "recv";
            // std::string peer_or_master = info.rank == 0 ? "master" : "peer";
            // _cout << "P[" << world_rank << "] completed " << type << " request for " << peer_or_master 
            //           << " with rank " << info.rank << " buffer ptr: " << (void*)info.buffer << std::endl;
    
            const int rank = status.MPI_TAG;
            const int real_source = is_master_msg ? MASTER_RANK : status.MPI_SOURCE + 1; 

            int count;
            MPI_Get_count(&status, MPI_FLOAT, &count);
            // _cout << "P[" << world_rank << "] received data from worker " << real_source 
            //           << " for rank " << rank << " with count " << count 
            //           << (is_master_msg ? " (master message)" : " (peer message)") 
            //           << std::endl;

            if (count == 0) {
                // _cout << "P[" << world_rank << "] received termination signal from worker " << real_source << " for rank " << rank
                // << (node_state.lower_priority_sibling_terminated(rank) ? " (lower priority siblings terminated)" : " (NOT lower priority siblings terminated)")
                // // << (node_state.is_lowest_priority_sibling(rank) ? " (this is the lowest priority sibling)" : " (this is NOT the lowest priority sibling)")
                // << (node_state.input_master_terminated(rank) ? " (input master terminated)" : " (input master NOT terminated)")
                // << std::endl;
                if (real_source == MASTER_RANK) {
                    // std::cout << "P[" << world_rank << "] SET EVNT: event_clear_master_recvs = true" << std::endl;
                    event_clear_master_recvs = true;
                }


                node_state.add_termination(rank, real_source); // Add termination for this rank

                _cout << "P[" << world_rank << "] added termination for worker " << real_source 
                          << " in rank " << rank << std::endl;

                if (node_state.in_comunication_closed(rank)) 
                {
        
                    const auto [flush_to, flush_rank] = node_state.where_to_flush(real_source, rank);
                    
                    if (flush_to == NodeState::END_OF_PROGRAM) {
                        _cout << "THIS SHOULD NOT HAPPEN: P[" << world_rank << "] flushing to END_OF_PROGRAM for rank " << rank << std::endl;
                        MPI_Abort(MPI_COMM_WORLD, EXIT_SUCCESS);
                    } else if (flush_to == node_state.node_id) {
                        assert(node_state.has_no_outputs(rank + 1) && "Node should not have outputs when flushing to itself");
                        _cout << "PROGRAM ENDED: TODO COMPUTE FINAL CORESET" << std::endl;
                    } else {
                        float *coreset_buff = coresets[rank];
                        const size_t coreset_size = rank == 0 ? CORESET_POINTS_SIZE : CORESET_TOTAL_SIZE;
                        // const MPI_Comm comm = flush_to == 0 ? MPI_COMM_WORLD : worker_comm;
                        // const int dest = flush_to == 0 ? MASTER_RANK : flush_to - 1; 

                        if (coreset_buff) {
                            std::cout << "[" << world_rank << ", " << rank << "] ==> [" << flush_to << ", " << flush_rank << "]" << std::endl;
                     
                            // std::cout << "P[" << world_rank << "] flushing coreset for rank " << rank 
                            //           << " to worker " << flush_to 
                            //           << " (flush rank: " << flush_rank << ")" << std::endl;

                            issue_send_peer_req(flush_to, flush_rank, coreset_buff, coreset_size);
                            coresets[rank] = nullptr; 
                        } else {
                            std::cout << "[" << world_rank << ", " << rank << "] --> [" << flush_to << ", " << flush_rank << "]" << std::endl;
                        }

                        // std::cout << "P[" << world_rank << "] Sending termination signal to worker " << flush_to 
                        //           << " (flush rank: " << flush_rank << ")" << std::endl;
                        issue_send_peer_req(flush_to, flush_rank, nullptr, 0);
                    }
                    
                    if (node_state.all_in_comunication_closed()) {
                        // std::cout << "P[" << world_rank << "] SET EVNT: event_clear_peers_recvs = true" << std::endl;
                        event_clear_peers_recvs = true; 
                    }
                }
            }

            //Reissue a new request for the MASTER if not closed rank0
            if (is_master_msg && !node_state.has_received_master_termination()) {
                issue_master_req();
            } else if (!is_master_msg && !node_state.all_in_comunication_closed()) {
                issue_peers_req();
            }

            if (count == 0)
                return; 

            // if rank 0 count must be CORESET_SIZE * features else CORESET_SIZE * features + CORESET_SIZE
            assert((rank == 0 && count == CORESET_SIZE * features) || (rank != 0 && count == CORESET_SIZE * features + CORESET_SIZE) && "Received data size should match expected size");

            // check if we received a rank outside of the node ranks
            if(!node_state.has_first_class_rank_support(rank)) {
                std::cout << "\n\n\tTODO implement handling of ranks outside of the node ranks. (Flushed from master rank node)\n" << std::endl;
                delete[] info.buffer;
                return; 
            }

            // if (node_state.in_comunication_closed(rank)) {
            //     std::cout << "P[" << world_rank << "] received data from worker " << real_source 
            //               << " for rank " << rank << " but the node is in communication closed state." 
            //               << std::endl;
            
            //     usleep(1000); // Sleep for a second to avoid busy waitinga
            //     assert(false);
            // } 

            // assert(!node_state.in_comunication_closed(rank));

            if (rank == 0 && is_master_msg) {
                STAT_RANK0_CORESETS++;
            }

            // check if real source is master rank for rank n - 1 
            if (rank > 0 && node_state.is_rank_master(rank - 1, real_source)) {
                std::cout << "Flush from " << real_source << " for rank " << rank - 1 << " to worker " << world_rank  << std::endl;
            }
                


            if (coresets[rank] == nullptr) {
                coresets[rank] = info.buffer;
                return; 
            }

            _cout << "P[" << world_rank << "] merging coreset for rank " << rank << std::endl; 
            
            int curr_rank = rank;

            float *running_coreset = info.buffer;
            do {
                const bool with_weights = curr_rank != 0;

                init_merge_buffer(merge_buffer, 
                    coresets[curr_rank],
                    running_coreset, 
                    curr_rank != 0,
                    curr_rank != 0,
                    CORESET_SIZE, features);

                STAT_AGGS++;
                if (curr_rank == 1) {
                    STAT_RANK1_AGGS++;
                } else if (curr_rank == 0) {
                    STAT_RANK0_AGGS++;
                }

                auto ctree = CoresetTree(merge_buffer, 
                    with_weights ? merge_buffer + CORESET_POINTS_SIZE * 2 : nullptr,
                    CORESET_SIZE * 2, features, CORESET_SIZE);
                
                ctree.extract_raw_inplace(
                    coreset_extract_buffer, 
                    coreset_extract_buffer + CORESET_POINTS_SIZE, 
                    CORESET_SIZE, features
                );

                delete[] coresets[curr_rank]; 
                coresets[curr_rank] = nullptr;

                curr_rank++;
                running_coreset = coreset_extract_buffer; // Update the running coreset to the extracted one
            } while (
                node_state.has_first_class_rank_support(curr_rank) &&
                coresets[curr_rank]
            );
            assert(running_coreset != info.buffer && "Running coreset should not be the same as the received buffer");

            coreset_extract_buffer = new float[CORESET_TOTAL_SIZE];
            
            delete[] info.buffer; // Free the received buffer
                            
            if (!node_state.has_first_class_rank_support(rank + 1)) {
                int next_worker = randomRankWorker(rank + 1, rank_map);
                issue_send_peer_req(next_worker, rank + 1, running_coreset, CORESET_TOTAL_SIZE);
            }
        };

        perf.resume(); // Resume perf after initialization

        // start by initiating recvs
        // Master request
        const int PENDING_MASTER_REQUESTS = 1; // Master requests
        for (int i = 0; i < PENDING_MASTER_REQUESTS; ++i) 
            issue_master_req();

        // Peer Requests
        const int PENDING_PEER_REQUESTS = 1; 
        for (int i = 0; i < PENDING_PEER_REQUESTS; ++i)
            issue_peers_req();

        // set random seed for the worker
        srand(time(nullptr) + world_rank); // Seed the random number generator with the current time and rank
        while (!master_requests.empty() || !peer_requests.empty())
        {   
            master_requests_indices.resize(master_requests.size());
            peer_requests_indices.resize(peer_requests.size());
            master_request_statuses.resize(master_requests.size());
            peer_request_statuses.resize(peer_requests.size());


            assert(
                static_cast<int>(peer_requests.empty()) <= static_cast<int>(master_requests.empty()) 
            );

            int outcount = 0;
            if (!peer_requests.empty()) {
                MPI_Testsome(peer_requests.size(), peer_requests.data(), &outcount, peer_requests_indices.data(), peer_request_statuses.data());
                assert(outcount != MPI_UNDEFINED && "MPI_Testsome returned MPI_UNDEFINED, which should not happen here");
                if (outcount > 0) {
                    for (int i = 0; i < outcount; ++i) { // Foreach completed request
                        int idx = peer_requests_indices[i];
                        handle_recv(false, peer_request_info[idx], peer_request_statuses[i]);
                    }
    
                    remove_indices(std::span<int>(peer_requests_indices.data(), outcount), peer_requests, peer_request_info);
                }
            }

            if (!master_requests.empty()) {
                MPI_Testsome(master_requests.size(), master_requests.data(), &outcount, master_requests_indices.data(), master_request_statuses.data());
                assert(outcount != MPI_UNDEFINED && "MPI_Testsome returned MPI_UNDEFINED, which should not happen here");
                if (outcount > 0) {
                    for (int i = 0; i < outcount; ++i) { // Foreach completed request
                        int idx = master_requests_indices[i];
                        handle_recv(true, master_request_info[idx], master_request_statuses[i]);
                    }

                    remove_indices(std::span<int>(master_requests_indices.data(), outcount), master_requests, master_request_info);
                }
            }

            if (event_clear_peers_recvs) {
                // std::cout << "P[" << world_rank << "] clearing peer recvs." << std::endl;
                for (size_t i = 0; i < peer_requests.size(); ++i) 
                    if (!peer_request_info[i].is_send_req) 
                        MPI_Cancel(&peer_requests[i]); 

                event_clear_peers_recvs = false; 
            }

            if (event_clear_master_recvs) {
                // std::cout << "P[" << world_rank << "] clearing master recvs. to clear: " << master_requests.size() << std::endl;

                for (size_t i = 0; i < master_requests.size(); ++i) 
                    MPI_Cancel(&master_requests[i]); 

                event_clear_master_recvs = false; 
            }
        }
    
        // _cout << "P[" << world_rank << "] delete[] merge_buffer: " << (void*)merge_buffer << std::endl;
        delete[] merge_buffer;
        delete[] coreset_extract_buffer; // Free the coreset extract buffer

        size_t total_core_sets = 0;
        size_t total_aggregations = 0;
        size_t total_rank0_aggregations = 0;
        size_t total_rank1_aggregations = 0;
        MPI_Reduce(&STAT_AGGS, &total_aggregations, 1, MPI_UNSIGNED_LONG, MPI_SUM, MASTER_RANK, worker_comm);
        MPI_Reduce(&STAT_RANK0_CORESETS, &total_core_sets, 1, MPI_UNSIGNED_LONG, MPI_SUM, MASTER_RANK, worker_comm);
        MPI_Reduce(&STAT_RANK1_AGGS, &total_rank1_aggregations, 1, MPI_UNSIGNED_LONG, MPI_SUM, MASTER_RANK, worker_comm);
        MPI_Reduce(&STAT_RANK0_AGGS, &total_rank0_aggregations, 1, MPI_UNSIGNED_LONG, MPI_SUM, MASTER_RANK, worker_comm);


        MPI_Barrier(worker_comm); // Ensure all ranks are synchronized before exiting
        
        std::cout << "P[" << world_rank << "] Exit. STAT_AGGS: " << STAT_AGGS 
                  << ", STAT_RANK0_CORESETS: " << STAT_RANK0_CORESETS 
                  << ", STAT_RANK0_AGGS: " << STAT_RANK0_AGGS 
                  << ", STAT_RANK1_AGGS: " << STAT_RANK1_AGGS 
                  << std::endl;

        usleep(1000); // Sleep for a second to allow all ranks to finish printing
        MPI_Barrier(worker_comm); // Ensure all ranks are synchronized before exiting


        if (world_rank == 1) {

            std::cout 
                    << "P[" << world_rank << "] Final statistics: "
                    << "\n\tTotal aggregations performed: " << total_aggregations
                    << " \n\tTotal rank 0 coresets received: " << total_core_sets
                    << " \n\tTotal rank 0 aggregations performed: " << total_rank0_aggregations
                    << " \n\tTotal rank 1 aggregations performed: " << total_rank1_aggregations 
            << std::endl;


            // print coresets stored 
            std::cout << "Coresets stored: " << std::endl;
            for (size_t r = 0; r < coresets.size(); ++r) {
                if (coresets[r] != nullptr) {
                    std::cout << "\t Rank " << r << ": yes" << std::endl;
                } 
            }
        } else {
            // ALL RANKS MUST BE EMPTY
            std::string coreset_str = "Coresets rank: " + std::to_string(world_rank) + ": ";
            for (size_t r = 0; r < coresets.size(); ++r) {
                if (coresets[r] != nullptr) {
                    coreset_str += std::to_string(r) + " ";
                    // std::cerr << "P[" << world_rank << "] ERROR: Coreset for rank " << r << " in worker " << world_rank 
                    //           << " is not empty! This should not happen." << std::endl;
                    // MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
                }
            }
            std::cout << coreset_str << std::endl;
        }


        MPI_Comm_free(&worker_comm); // Free the worker communicator
    }

    

    perf.pause(); // Pause perf after sending all data
    MPI_Finalize();
}