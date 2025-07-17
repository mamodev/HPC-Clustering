#pragma once

// #include "coreset_naive.hpp"
// #include "coreset_opt1.hpp"
// #include "coreset_opt2.hpp"
#include "coreset_opt3.hpp"
#include "coreset_plot.hpp"
#include "assert.hpp"

std::vector<float> compute_final_coreset(std::vector<std::vector<float>>& buckets, size_t features, size_t coreset_size) {
    auto coreset = std::vector<float>();
    for (auto& bucket : buckets) {
        if (bucket.size() > 0) {
            coreset.insert(coreset.end(), bucket.begin(), bucket.end());
        }
    }

    fassert(coreset.size() > 0, "Coreset should not be empty");

    size_t n_points = coreset.size() / (features + 1);
    if (n_points >= coreset_size * 2) {
        auto reduced_coreset = Coreset<size_t, true, 3, 10U>(coreset.data(), n_points, features, coreset_size);
        coreset = std::move(reduced_coreset);
    }
    
    coreset.resize(coreset_size * (features + 1));
    fassert(coreset.size() / (features + 1) == coreset_size, 
    "Final coreset size should be equal to coreset_size");

    return coreset;
}

