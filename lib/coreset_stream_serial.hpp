#pragma once
#include <vector>
#include "assert.hpp"
#include "parser.hpp"
#include "coreset.hpp"

template<typename int_t = std::size_t, int MinSplitIters=3, unsigned int Seed = 0U>
std::vector<float> coresetStreamSerial(CoresetStream& stream) {
    
    std::vector<std::vector<float>> buckets;

    auto batch = stream.next_batch();
    while(!batch.empty()) {
        auto coreset = Coreset<int_t, false, MinSplitIters, Seed>(batch.data(), batch.size() / stream.features, stream.features, stream.coreset_size);

        size_t next = 0;
        while(buckets.size() > next && !buckets[next].empty()) { 
            std::vector<float>& bucket_coreset_vec = buckets[next]; 
            coreset.insert(coreset.end(), bucket_coreset_vec.begin(), bucket_coreset_vec.end());
            size_t merged_n_points = coreset.size() / (stream.features + 1); // Assuming weights added
        
            coreset = std::move(
                Coreset<int_t, true, MinSplitIters, Seed>(coreset.data(), merged_n_points, stream.features, stream.coreset_size)
            );
            
            bucket_coreset_vec.clear(); // Mark bucket as empty
            next++;
        }

        if(next >= buckets.size()) {
            buckets.push_back(std::move(coreset));
        } else {
            buckets[next] = std::move(coreset);
        }

        batch = stream.next_batch();
    }   


    auto coreset = std::vector<float>();
    for (auto& bucket : buckets) {
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