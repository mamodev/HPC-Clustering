#pragma once

#include <vector>
#include <omp.h>

#include "coreset.hpp"
#include "assert.hpp"

template<typename int_t = std::size_t, int MinSplitIters=3, unsigned int Seed = 0U>
std::vector<float> coresetStreamOmp(CoresetStream& stream, int num_threads = -1) {
    const size_t coreset_size = stream.coreset_size;
    const size_t features     = stream.features;

    if (num_threads < 1) {
        num_threads = omp_get_max_threads();
    }

    std::vector<std::vector<std::vector<float>>> buckets;
    #pragma omp parallel num_threads(num_threads)
    {

        bool stream_finished = false;
        while (true) {
            int task_rank = -1;
            std::vector<float> c1, c2;

            #pragma omp critical(bucket_lock)
            {
                for (int rank = buckets.size() - 1; rank > 0; --rank) {
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

                task_rank = 0; 
                if (c1.empty()) break; 
            }

            // --- Phase 3: Execute the work (NO LOCKS HELD) ---
            std::vector<float> result_coreset;
            if (task_rank == 0) { // New batch from stream
                result_coreset = Coreset<int_t, false, MinSplitIters, Seed>(
                    c1.data(), c1.size() / stream.features,
                    stream.features, stream.coreset_size
                );
            } else { // Merging two existing coresets
                c1.insert(c1.end(), c2.begin(), c2.end());
                result_coreset = Coreset<int_t, true, MinSplitIters, Seed>(
                    c1.data(), c1.size() / (stream.features + 1),
                    stream.features, stream.coreset_size
                );
            }

            task_rank++;

            // --- Phase 4: Store the result ---
            #pragma omp critical(bucket_lock)
            {
                if (buckets.size() <= task_rank) 
                    buckets.resize(task_rank + 1);

                buckets[task_rank].push_back(std::move(result_coreset));
            }
        } // End of while loop
    } // End of parallel region (implicit barrier)


    fassert(buckets[0].size() == 0, "Bucket 0 should be empty at the end of processing, instead it has " + std::to_string(buckets[0].size()) + " coresets");    

    // reduce all coresets in buckets[1]
    for (int rank = 2; rank < buckets.size(); ++rank) {
        if (buckets[rank].empty()) continue;
        buckets[1].insert(buckets[1].end(), std::make_move_iterator(buckets[rank].begin()), std::make_move_iterator(buckets[rank].end()));
        buckets[rank].clear();
    }

    auto coreset = std::vector<float>();
    for (auto& bucket : buckets[1]) {
        if (bucket.size() > 0) {
            coreset.insert(coreset.end(), bucket.begin(), bucket.end());
            bucket.clear(); // Clear the bucket after merging
        }
    }

    fassert(coreset.size() > 0, "Coreset should not be empty");

    size_t n_points = coreset.size() / (stream.features + 1);
    if (n_points > stream.coreset_size) {
        auto reduced_coreset = Coreset<size_t, true, 3, 10U>(coreset.data(), n_points, stream.features, stream.coreset_size);
        coreset = std::move(reduced_coreset);
    }
    
    fassert(coreset.size() / (stream.features + 1) == stream.coreset_size, 
    "Final coreset size should be equal to coreset_size");

    return coreset;
}