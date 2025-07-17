#include <iostream>
#include <random>

#include <vector>
#include <variant>
#include <tuple>
#include <span>
#include <set>
#include <functional>

#include <chrono>
#include "perf.hpp"
#include "assert.hpp"
#include "parser.hpp"

// #define USE_PYTHON_PLOT
#include "coreset.hpp"

#include <cstdlib>


int main(int argc, char** argv) {

    auto plotter = CoresetPlotter();

    auto perf = PerfManager();
    perf.pause(); 

    MemoryStream stream(argc, argv);

    // auto [samples, outDir, coreset_size] = parseArgs<float>(argc, argv);

    // size_t batches = samples.samples / coreset_size;
    // std::cout << "Coreset size: " << coreset_size << std::endl;
    // std::cout << "Samples: " << samples.samples << std::endl;
    // std::cout << "Features: " << samples.features << std::endl;
    // std::cout << "Batches: " << batches << std::endl;
    

    perf.resume();
    auto start = std::chrono::high_resolution_clock::now();
    
    
    auto final_coreset = std::vector<float>();

    // for (int r = 0; r < 1; ++r) {

    size_t computations_per_rank[100] = {0};
    size_t computed = 0;

    std::vector<std::vector<float>> buckets;

    auto batch = stream.next_batch();
    while(!batch.empty()) {


        // std::cout << "batch size: " << batch_size << std::endl;

        auto coreset = Coreset<size_t, false, 3, 3U>(batch.data(), batch.size() / stream.features, stream.features, stream.coreset_size);
        computations_per_rank[0] += 1;
        computed += 1;
        // std::cout << "corest size: " << coreset.size() << std::endl;

        size_t next = 0;
        while(buckets.size() > next && !buckets[next].empty()) { // Use !buckets[next].empty() for clarity
            // No more new/delete[] or manual loops!
            std::vector<float>& bucket_coreset_vec = buckets[next]; // Get reference to the vector in bucket
        
            // Merge current_coreset_vec into bucket_coreset_vec (or vice-versa, depending on desired outcome)
            // For simplicity, let's merge bucket_coreset_vec into current_coreset_vec:
            coreset.insert(coreset.end(), bucket_coreset_vec.begin(), bucket_coreset_vec.end());
        
            size_t merged_n_points = coreset.size() / (stream.features + 1); // Assuming weights added
        
            coreset = std::move(
                Coreset<size_t, true, 3, 3U>(coreset.data(), merged_n_points, stream.features, stream.coreset_size)
            );
            computed += 1;
            computations_per_rank[next + 1] += 1;

            
            bucket_coreset_vec.clear(); // Mark bucket as empty
            next++;
        }

        // while(buckets.size() > next && buckets[next].size() != 0) {

        //     size_t points_merged_size = coreset.size() + buckets[next].size();
        //     float *points_merged = new float[points_merged_size];
        //     for (int i = 0; i < coreset.size(); ++i) points_merged[i] = coreset[i];
        //     for (int i = 0; i < buckets[next].size(); ++i) points_merged[i + coreset.size()] = buckets[next][i];

        //     size_t merged_n_points = points_merged_size / (stream.features + 1);

        //     // std::cout << "Merging bucket " << next << " with size: " << buckets[next].size() << std::endl;
        //     // std::cout << "mergin " << points_merged_size << std::endl;
        //     coreset = std::move(
        //         Coreset<size_t, true, 3, 3U>(points_merged, merged_n_points, stream.features, coreset_size)
        //     );
            
        //     delete[] points_merged;
        //     buckets[next] = std::vector<float>();
        //     next++;
        // }


        if(next >= buckets.size()) {
            buckets.push_back(std::move(coreset));
        } else {
            buckets[next] = std::move(coreset);
        }

        batch = stream.next_batch();
        // plotter.plot(compute_final_coreset(buckets, stream.features, coreset_size), stream.features);
    }   


        for (int r = 0; r < buckets.size(); ++r) {
            std::cout << " ," << computations_per_rank[r];
        }
        std::cout << std::endl;



        // final_coreset = std::move(compute_final_coreset(buckets, stream.features, coreset_size)); 
        // plotter.plot(final_coreset, stream.features);
    // }


    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    perf.pause();
    std::cout << "Coreset computed in " << duration << " ms" << std::endl;
    std::cout << "Computed " << computed << " coreset batches." << std::endl;

    return 0;
}

