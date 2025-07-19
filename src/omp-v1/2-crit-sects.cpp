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

    
//    omp_set_num_threads(
//        std::max(1u, std::thread::hardware_concurrency()/2u)
//    );

int main(int argc, char* argv[]) {
    auto perf = PerfManager();
    perf.pause();
    MemoryStream<false> stream(argc, argv);
    const size_t coreset_size = stream.coreset_size;
    const size_t features     = stream.features;

    perf.resume();
    auto start = std::chrono::high_resolution_clock::now();
    std::vector<std::vector<std::vector<float>>> buckets;
    #pragma omp parallel
    {
        bool stream_finished = false;
        while (true) {
            int task_rank = -1;
            std::vector<float> c1, c2;

            #pragma omp critical(bucket_lock)
            {
                for (int rank = buckets.size() - 1; rank >= 0; --rank) {
                    if (buckets[rank].size() >= 2) {
                        c1 = std::move(buckets[rank].back());
                        buckets[rank].pop_back();
                        c2 = std::move(buckets[rank].back());
                        buckets[rank].pop_back();
                        task_rank = rank;
                        break;
                    }
                }
            } 

            if (c1.empty() && !stream_finished) {
                #pragma omp critical(stream_lock)
                {
                    c1 = std::move(stream.next_batch());
                }

                if (c1.empty()) break; 

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

            // --- Phase 4: Store the result ---
            #pragma omp critical(bucket_lock)
            {
                size_t result_rank = (task_rank == -1) ? 0 : task_rank + 1;
                if (buckets.size() <= result_rank) {
                    buckets.resize(result_rank + 1);
                }
                buckets[result_rank].push_back(std::move(result_coreset));
            }
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
