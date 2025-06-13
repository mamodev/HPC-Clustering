#include <optional>
#include <mpi.h>
#include <set>
#include "CoresetTree.hpp"
#include "CoresetStream.hpp"
#include "parser.hpp"
#include "perf.hpp"

#define MASTER_RANK 0

#if !defined(CORESET_SIZE)
#define CORESET_SIZE 60
#endif

#if !defined(CLUSTERS)
#define CLUSTERS 3
#endif

#include <cstddef>

constexpr uint32_t steps (uint32_t workers) {
    assert(workers > 0 && "workers must be greater than 0");
    return static_cast<uint32_t>(std::floor(std::log2(workers)) + 1);
}

constexpr uint32_t workers_for_step(uint32_t rank, uint32_t workers) {
    assert(workers > 0 && "workers must be greater than 0");
    assert(rank < workers && "rank must be less than workers");
    return static_cast<uint32_t>(
        std::round(workers / std::pow(2, rank + 1))
    );
}

constexpr uint32_t worker_step(uint32_t workers, uint32_t worker_id) {
    assert(workers > 0 && "workers must be greater than 0");
    assert(worker_id < workers && "worker_id must be less than workers");

    uint32_t s = 0;
    for (s = 0; s < steps(workers); ++s) {
        if (worker_id < workers_for_step(s, workers)) {
            break;
        }

        worker_id -= workers_for_step(s, workers);
    }

    return s;
}

constexpr uint32_t worker_step_id(uint32_t workers, uint32_t worker_id) {
    assert(workers > 0 && "workers must be greater than 0");
    assert(worker_id < workers && "worker_id must be less than workers");

    uint32_t s = worker_step(workers, worker_id);
    uint32_t worker_count = 0;

    for (uint32_t i = 0; i < s; ++i) {
        worker_count += workers_for_step(i, workers);
    }

    return worker_id - worker_count;
}

constexpr uint32_t sid_to_wid(uint32_t workers, uint32_t step, uint32_t step_id) {
    assert(workers > 0 && "workers must be greater than 0");
    assert(step < steps(workers) && "step must be less than steps(workers)");
    assert(step_id < workers_for_step(step, workers) && "step_id must be less than workers_for_step(step, workers)");

    uint32_t worker_count = 0;
    for (uint32_t i = 0; i < step; ++i) {
        worker_count += workers_for_step(i, workers);
    }

    return worker_count + step_id;
}
void master(int argc, char **argv, int world_size) {
    auto [samples, outdir, _] = parseArgs<float>(argc, argv);

    char outdir_cstr[255];
    std::strncpy(outdir_cstr, outdir.c_str(), sizeof(outdir_cstr) - 1);
    outdir_cstr[sizeof(outdir_cstr) - 1] = '\0'; // Ensure null termination
    MPI_Bcast(outdir_cstr, 255, MPI_CHAR, MASTER_RANK, MPI_COMM_WORLD);

    DeltaTimer timer;


    uint32_t features = samples.features;
    MPI_Bcast(&features, 1, MPI_UINT32_T, MASTER_RANK, MPI_COMM_WORLD);

  

    auto get_chunk_at = [&](size_t cursor) -> const float* {
        size_t wrapped_cursor = cursor % samples.samples;
        if (wrapped_cursor + CORESET_SIZE > samples.samples) {
            wrapped_cursor = samples.samples - CORESET_SIZE; // Ensure we don't go out of bounds
        }

        return samples.data.data() + wrapped_cursor * samples.features; // Get the chunk of data at the cursor
    };
    
    int first_layer = workers_for_step(0, world_size - 1);
    int next_worker = 0;
    
    size_t stream_cursor = 0;
    size_t stream_size = 10 * samples.samples; 

    std::vector<MPI_Request> requests;
    std::vector<MPI_Status> statuses; // Statuses for the requests
    std::vector<int> request_indices; // Indices of completed requests
    std::vector<std::function<void(void)>> stream_send_timers;

    assert(stream_size / CORESET_SIZE >= first_layer && "Stream size must be greater than or equal to CORESET_SIZE * first_layer");

 
    for (int w = 0; w < first_layer; ++w) {
        const float *chunk = get_chunk_at(stream_cursor);
        stream_cursor += CORESET_SIZE;
        MPI_Isend(chunk, CORESET_SIZE * samples.features, MPI_FLOAT, w + 1, 0, MPI_COMM_WORLD, &requests.emplace_back());
        stream_send_timers.emplace_back(timer.start("stream-send"));
    }

    statuses.resize(requests.size());
    request_indices.resize(requests.size());

    std::set<int> eof_idx;
    auto stream_t = timer.start("stream");
    auto noop = [](){};

    while (true) {        
        int outcount = 0;
        MPI_Waitsome(requests.size(), requests.data(), &outcount, request_indices.data(), statuses.data());

        if (outcount == MPI_UNDEFINED) {
            std::cout << "SENT ALL MESSAGES TO WORKERS" << std::endl;
            break; 
        }

        assert(outcount > 0 && "mpi_waitsome returned negative outcount or zero, which should not happen here");
        for (int r = 0; r < outcount; ++r) {
            int idx = request_indices[r];

            stream_send_timers[idx](); 
            stream_send_timers[idx] = noop;

            if (eof_idx.contains(idx)) {
                continue;
            }


            if (stream_cursor + CORESET_SIZE > stream_size) {
                eof_idx.insert(idx);
                MPI_Isend(nullptr, 0, MPI_FLOAT, idx + 1, 0, MPI_COMM_WORLD, &requests[idx]);

            } else {
                stream_send_timers[idx] = timer.start("stream-send");
                const float *chunk = get_chunk_at(stream_cursor);
                stream_cursor += CORESET_SIZE;
                MPI_Isend(chunk, CORESET_SIZE * samples.features, MPI_FLOAT, idx + 1, 0, MPI_COMM_WORLD, &requests[idx]);
            }
        }
    }

    stream_t();



    std::cout << "Master process ended scattering data to workers." << std::endl;

    for (int w = 0; w < first_layer; ++w) {
        MPI_Send(nullptr, 0, MPI_FLOAT, w + 1, 0, MPI_COMM_WORLD); // Send termination signal
    }

    timer.to_file(outdir + "/delta_master.csv");
}

void worker(MPI_Comm &workers) {
    DeltaTimer timer;

    int world_rank, world_size;
    MPI_Comm_size(workers, &world_size);
    MPI_Comm_rank(workers, &world_rank);

    std::cout << "W[" << world_rank << ", (pid: " << getpid() << ")] started." << std::endl;

    char outdir[255];
    MPI_Bcast(outdir, 255, MPI_CHAR, MASTER_RANK, MPI_COMM_WORLD);
    
    uint32_t features;
    MPI_Bcast(&features, 1, MPI_UINT32_T, MASTER_RANK, MPI_COMM_WORLD);

    const uint32_t STEP = worker_step(world_size, world_rank);

    const size_t POINTS_SIZE = CORESET_SIZE * features;
    const size_t WEIGHTS_SIZE = CORESET_SIZE;

    const size_t IN_SIZE = STEP == 0 ? POINTS_SIZE : POINTS_SIZE + WEIGHTS_SIZE;
    const size_t OUT_SIZE = POINTS_SIZE + WEIGHTS_SIZE;

    assert(IN_SIZE <= OUT_SIZE && "Input size must be less than output size");

    const MPI_Comm& IN_COMM = STEP == 0 ? MPI_COMM_WORLD : workers;


    const int NEXT_STEP_MIN_W = STEP + 1 < steps(world_size) ? sid_to_wid(world_size, STEP + 1, 0) : -1;
    const int NEXT_STEP_MAX_W = STEP + 1 < steps(world_size) ? sid_to_wid(world_size, STEP + 1, workers_for_step(STEP + 1, world_size) - 1) : -1;

    const int TARGET_TERMINATIONS = STEP == 0 ? 1 : workers_for_step(STEP - 1, world_size);

    bool stored_in[2] = { false, false };
    MPI_Request in[2], out = MPI_REQUEST_NULL;

    float *flat_in_vecs = new float[IN_SIZE * 4];
    std::span<float> in_vecs[4] = {
        std::span<float>(flat_in_vecs + 0 * IN_SIZE, IN_SIZE),
        std::span<float>(flat_in_vecs + 1 * IN_SIZE, IN_SIZE),
        std::span<float>(flat_in_vecs + 2 * IN_SIZE, IN_SIZE),
        std::span<float>(flat_in_vecs + 3 * IN_SIZE, IN_SIZE)
    };

    std::vector<float> out_vec(OUT_SIZE);

    MPI_Irecv(in_vecs[0].data(), IN_SIZE, MPI_FLOAT, MPI_ANY_SOURCE, STEP, IN_COMM, &in[0]);
    MPI_Irecv(in_vecs[1].data(), IN_SIZE, MPI_FLOAT, MPI_ANY_SOURCE, STEP, IN_COMM, &in[1]);

    srand(static_cast<unsigned int>(world_rank + 1));

    int terminations = 0;

    auto t_running = timer.start("running");
    auto t_l_running = timer.start("running-" + std::to_string(STEP));
    while ( true )  
    {   
        stored_in[0] = false;

        auto t_w_input = timer.start("w-input");
        auto t_lw_input = timer.start("w-input-" + std::to_string(STEP));
        while (!stored_in[0] || !stored_in[1]) {
            int index;
            MPI_Status status;
            int count;

            MPI_Waitany(2, in, &index, &status);
            assert(index != MPI_UNDEFINED && "Index must not be undefined");
            assert(index < 2 && "Index must be less than 2");
            
            MPI_Get_count(&status, MPI_FLOAT, &count);
            if (count != 0) {
                std::swap(in_vecs[index], in_vecs[2 + index]); // Swap the received vector with the next one to reuse it
                stored_in[index] = true;
            }

            if (count == 0)
                terminations++;

            MPI_Irecv(in_vecs[index].data(), IN_SIZE, MPI_FLOAT, MPI_ANY_SOURCE, STEP, IN_COMM, &in[index]);
            
            if (terminations >= TARGET_TERMINATIONS) 
                break;
        }
        t_w_input();
        t_lw_input();

        auto t_w_send = timer.start("w-send");
        auto t_lw_send = timer.start("w-send-" + std::to_string(STEP));
        // std::cout << "W[" << world_rank << "] processing data in step " << STEP << std::endl;
        if (out != MPI_REQUEST_NULL) {
            // std::cout << "W[" << world_rank << "] waiting for outstanding send request in step " << STEP << std::endl;
            MPI_Wait(&out, MPI_STATUS_IGNORE);
        }
        t_w_send();
        t_lw_send();

        if (!stored_in[0] || !stored_in[1])  // Termination condition and no data to flush
            break;

        if (stored_in[0] && stored_in[1]) { // Reduce -> out
            auto t_w_merge = timer.start("w-merge");
            auto t_lw_merge = timer.start("w-merge-" + std::to_string(STEP));

            float *merged_vec = new float[2 * POINTS_SIZE + 2 * WEIGHTS_SIZE];

            memcpy(merged_vec, in_vecs[2].data(), POINTS_SIZE * sizeof(float));
            memcpy(merged_vec + POINTS_SIZE, in_vecs[3].data(), POINTS_SIZE * sizeof(float));
            
            if (STEP != 0) { 
                memcpy(merged_vec + 2 * POINTS_SIZE, in_vecs[2].data() + POINTS_SIZE, WEIGHTS_SIZE * sizeof(float));
                memcpy(merged_vec + 2 * POINTS_SIZE + WEIGHTS_SIZE, in_vecs[3].data() + POINTS_SIZE, WEIGHTS_SIZE * sizeof(float));
            }

            auto root = CoresetTree(merged_vec, STEP == 0 ? nullptr : merged_vec + 2 * POINTS_SIZE, 2 * CORESET_SIZE, features, CORESET_SIZE);
            root.extract_raw_inplace(out_vec.data(), out_vec.data() + POINTS_SIZE, CORESET_SIZE, features);

            delete[] merged_vec;

            t_lw_merge();
            t_w_merge();
        } else {
            int idx = stored_in[0] ? 0 : 1;
            memcpy(out_vec.data(), in_vecs[idx].data(), in_vecs[idx].size() * sizeof(float));
            if (IN_SIZE != OUT_SIZE) {
                memset(out_vec.data() + IN_SIZE, 0, (OUT_SIZE - IN_SIZE) * sizeof(float)); 
            }
        }


        if (NEXT_STEP_MIN_W != -1 && NEXT_STEP_MAX_W != -1) {
            int next_worker = NEXT_STEP_MIN_W + rand() % (NEXT_STEP_MAX_W - NEXT_STEP_MIN_W + 1);
            // std::cout << "W[" << world_rank << "] sending data to worker " << next_worker << " in step " << STEP + 1 << std::endl;
            MPI_Isend(out_vec.data(), OUT_SIZE, MPI_FLOAT, next_worker, STEP + 1, workers, &out);
        }

        if (!stored_in[0] || !stored_in[1]) {  // Termination and data flushed
            MPI_Wait(&out, MPI_STATUS_IGNORE);
            break;
        }
    }
   



    if (NEXT_STEP_MIN_W != -1 && NEXT_STEP_MAX_W != -1) {
        for (int i = NEXT_STEP_MIN_W; i <= NEXT_STEP_MAX_W; ++i) {
            std::cout << "W[" << world_rank << "] sending termination signal to worker " << i << " in step " << STEP + 1 << std::endl;
            MPI_Isend(nullptr, 0, MPI_FLOAT, i, STEP + 1, workers, &out);
            MPI_Wait(&out, MPI_STATUS_IGNORE);
        }
    }
    
    t_running();
    t_l_running();
        
    delete[] flat_in_vecs;
    std::cout << "Worker " << world_rank << " in step " << STEP << " finished with " << terminations << " terminations." << std::endl;

    std::string perf_file = std::string(outdir) + "/delta_w_" + std::to_string(world_rank) + ".csv";
    timer.to_file(perf_file);
}

int main(int argc, char **argv)
{
    auto perf = PerfManager();
    perf.pause(); // Pause perf at the start

    MPI_Init(&argc, &argv);

    int world_rank, world_size;
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);

    assert(world_size < std::numeric_limits<uint32_t>::max() && "Number of processes must be less than INT_MAX");

    MPI_Comm workers;
    MPI_Comm_split(MPI_COMM_WORLD, world_rank == 0 ? 0 : 1, world_rank, &workers);

    if (world_rank == 0) {

        const uint32_t steps_count = steps(world_size - 1);
        uint32_t step_workers = 0;
        for (uint32_t i = 0; i < steps_count; ++i) {
            step_workers += workers_for_step(i, world_size - 1);
        }

        assert(step_workers == world_size - 1 && "Total workers assigned to steps must equal total number of workers");

        std::cout << "Master process initialized with " << world_size - 1 << " workers." << std::endl;
        std::cout << "Total steps: " << steps(world_size - 1) << std::endl;

        // for (int s = 0; s < steps(world_size - 1); ++s) {
        //     std::cout << "Step " << s << ": " << workers_for_step(s, world_size - 1) << " workers." << std::endl;
        // }

        std::vector<std::vector<uint32_t>> step_workers_list;
        for (uint32_t s = 0; s < steps_count; ++s) {
            std::vector<uint32_t> step_workers;
            for (uint32_t i = 0; i < workers_for_step(s, world_size - 1); ++i) {
                step_workers.push_back(sid_to_wid(world_size - 1, s, i));
                assert(worker_step_id(world_size - 1, sid_to_wid(world_size - 1, s, i)) == i && "Worker step ID must match expected value");
            }
            step_workers_list.push_back(step_workers);
        }

        for (uint32_t s = 0; s < steps_count; ++s) {
            size_t expected_count = workers_for_step(s, world_size - 1);
            std::cout << "Step " << s << " (" << expected_count << "): ";

            for (const auto &worker : step_workers_list[s]) {
                std::cout << " " << worker;
            }
            std::cout << std::endl;

            assert(step_workers_list[s].size() == expected_count && "Step workers list size must match expected count");

        }
        
        perf.resume(); 
        master(argc, argv, world_size);
        perf.pause(); 


    } else {
        perf.resume();
        worker(workers);
        perf.pause(); 

        MPI_Comm_free(&workers);
    }
   
    MPI_Finalize();

}