#include <omp.h>
#include <iostream>
#include <deque>
#include <vector>
#include <mutex>
#include <cassert>
#include <thread>
#include "parser.hpp"
#include "coreset.hpp"
#include "perf.hpp"
#include "topo.hpp"

    
//    omp_set_num_threads(
//        std::max(1u, std::thread::hardware_concurrency()/2u)
//    );

int main(int argc, char* argv[]) {
    auto perf = PerfManager();
    perf.pause();
    MemoryStream<true> stream(argc, argv);
    const size_t coreset_size = stream.coreset_size;
    const size_t features     = stream.features;

    CpuTopo cpu_topo = detect_cpu_topology(true, true);
    std::cout << "Detected CPU Topology:\n" << cpuTopoToString(cpu_topo) << std::endl;

    perf.resume();
    auto start = std::chrono::high_resolution_clock::now();
    
    omp_set_num_threads(getCpuCount(cpu_topo));

    constexpr size_t MAX_BUCKETS = 20;
    // std::vector<std::vector<std::vector<float>>> buckets;
    std::array<std::vector<std::vector<float>>, MAX_BUCKETS> buckets;
    std::array<omp_lock_t, MAX_BUCKETS> bucket_mutexes;
    for (size_t i = 0; i < MAX_BUCKETS; ++i) {
        omp_init_lock(&bucket_mutexes[i]);
    }

    std::vector<size_t> thread_pin_map = flat_thread_pin_map(cpu_topo);

    bool done = false;

    #pragma omp parallel
    {
        int tid = omp_get_thread_num();
        size_t core = thread_pin_map[tid];
        size_t node = node_of_thread_from_map(cpu_topo, thread_pin_map, tid);
        set_thread_affinity(core);
      
        #pragma omp barrier

        while (true) {
            int task_rank = -1;
            std::vector<float> c1, c2;

            if (cpu_topo.size() == 1 || node == 0) {
                for (int rank = buckets.size() - 1; rank > 0; --rank) {
                    omp_set_lock(&bucket_mutexes[rank]);
                    if (buckets[rank].size() >= 2) {
                        c1 = std::move(buckets[rank].back());
                        buckets[rank].pop_back();
                        c2 = std::move(buckets[rank].back());
                        buckets[rank].pop_back();
                        task_rank = rank;
                    }
                    
                    omp_unset_lock(&bucket_mutexes[rank]);
                    if (task_rank != -1) break;
                }
            }
          
            if (cpu_topo.size() == 1 || node != 0) {
                if (c1.empty()) {
                    c1 = std::move(stream.next_batch());
                    if (c1.empty()) {
                        done = true;
                        break; 
                    }
                }
            } else {
                if (done) {
                    break; // Exit if no more batches to process
                }

                if (c1.empty()) {
                    std::this_thread::yield();
                    continue; // No new batch, wait for next iteration
                }
            }


            // --- Phase 3: Execute the work (NO LOCKS HELD) ---
            std::vector<float> result_coreset;
            if (task_rank == -1) { // New batch from stream
                result_coreset = Coreset<size_t, false, 3, 3U>(
                    c1.data(), c1.size() / stream.features,
                    stream.features, stream.coreset_size
                );
            } else { // Merging two existing coresets
                c1.insert(c1.end(), c2.begin(), c2.end());
                result_coreset = Coreset<size_t, true, 3, 3U>(
                    c1.data(), c1.size() / (stream.features + 1),
                    stream.features, stream.coreset_size
                );
            }

            size_t result_rank = (task_rank == -1) ? 0 : task_rank + 1;
            fassert(result_rank < MAX_BUCKETS, "Result rank exceeds bucket size");
            
            omp_set_lock(&bucket_mutexes[result_rank]);                    
            buckets[result_rank].push_back(std::move(result_coreset));
            omp_unset_lock(&bucket_mutexes[result_rank]);

        } // End of while loop
    } // End of parallel region (implicit barrier)


    //std::cout << "Final bucket sizes:";
    //for (const auto& rank : buckets) {
      	//std::cout << " ," << rank.size();
    //}
    //std::cout << std::endl;

    // reduce all coresets in buckets[1]
    for (int rank = 2; rank < buckets.size(); ++rank) {
        if (buckets[rank].empty()) continue;
        buckets[1].insert(buckets[1].end(), std::make_move_iterator(buckets[rank].begin()), std::make_move_iterator(buckets[rank].end()));
        buckets[rank].clear();
    }


    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    perf.pause();
    std::cout << "Coreset computed in " << duration << " ms" << std::endl;
    std::cout << "Total processed batches: " << stream.processed_batches << std::endl;

  

    return 0;
}
