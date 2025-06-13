#pragma once


#include "CoresetTree.hpp"

#include <vector>
#include <limits>
#include <algorithm>
#include <cassert>
#include <random>


// This function returns just centroids
std::vector<float> kmeans(const Coreset& coreset, size_t k, size_t rounds, size_t batch_size) {
    const size_t N = coreset.points.n;
    const size_t D = coreset.points.d;


    std::vector<size_t> indices(N);
    for (size_t i = 0; i < N; ++i) {
        indices[i] = i;
    }

    std::vector<float> centroids(k * D);
    std::vector<float> counts(k, 0.0);

    // randomly select k centroids
    for (size_t i = 0; i < k; ++i) {
        size_t idx = __pick_random(N);
        for (size_t j = 0; j < D; ++j) {
            centroids[i * D + j] = coreset.points[idx][j];
        }
    }

    std::vector<int> batch_indices(batch_size, -1);

    auto dev = std::mt19937(std::random_device()());

    for (size_t r = 0; r < rounds; ++r) {
        std::shuffle(indices.begin(), indices.end(), dev);

        for (size_t b = 0; b < N; b += batch_size) {

            size_t end = std::min(N, b + batch_size);

            for (size_t bidx = b; bidx < end; ++bidx) {
                size_t idx = indices[bidx];
                const float* sample = coreset.points[idx];

                float min_dist = std::numeric_limits<float>::max();
                int best_cluster = -1;

                for (size_t j = 0; j < k; ++j) {
                    const float* centroid = centroids.data() + j * D;
                    
                    float dist = 0.0;
                    for (size_t d = 0; d < D; ++d) {
                        float diff = sample[d] - centroid[d];
                        dist += diff * diff;
                    }
                    
                    if (dist < min_dist) {
                        min_dist = dist;
                        best_cluster = j;
                    }
                }

                assert(best_cluster != -1 && "Best cluster is -1!");
                // batch_indices[bidx] = best_cluster;
                batch_indices[bidx - b] = best_cluster;
            }


            for (size_t bidx = b; bidx < end; ++bidx) {
                size_t idx = indices[bidx];
                const float* sample = coreset.points[idx];

                int best_cluster = batch_indices[bidx - b];

                counts[best_cluster] += coreset.weights[idx];

                const float eta = 1.0 / counts[best_cluster];
                const float one_minus_eta = 1.0 - eta;

                for (size_t d = 0; d < D; ++d) {
                    centroids[best_cluster * D + d] = one_minus_eta * centroids[best_cluster * D + d] + eta * sample[d];
                }
            }
        }
    }

    return centroids;
}