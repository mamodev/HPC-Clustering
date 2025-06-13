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

template<typename int_t = std::size_t>
void Coreset(float * const __restrict__ __points,  const int_t N, const int_t D, const int_t K) {

    // =============== [Types and constants] ================
    constexpr int_t NoneIDX = std::numeric_limits<int_t>::max();
    struct leaf_t { int_t centroid_idx, start, end; float cost; };

    // =============== [Global variables] ================

    std::vector<uint64_t> u_deltas;
    const auto points = std::span<float>(__points, N);
    
    float* const __restrict__ __distances = new float[N];
    const auto distances = std::span<float>(__distances, N);

    float leaf_cost_total = 0.0;
    std::vector<leaf_t> leafs;
    leafs.reserve(K);

    auto rng = std::mt19937();
    auto fdistr = std::uniform_real_distribution<float>(0.0, 1.0);
   
    // =============== [Initialization] ================
    {
        // auto dist = std::uniform_int_distribution<uint32_t>(0, N - 1);
        // uint32_t idx = dist(rng);
        int_t idx = 1234;

    
        float cost = 0.0;
        for (uint32_t i = 0; i < N; ++i) {
            float c = 0.0;
            for (uint32_t j = 0; j < D; ++j) {
                c += (points[i * D + j] - points[idx * D + j]) * 
                      (points[i * D + j] - points[idx * D + j]);
            }

            distances[i] = c;
            cost += c;
        }


        leafs.emplace_back(
            leaf_t{idx, 0, N - 1, cost}
        );

        leaf_cost_total = cost;
    }
    
    while (leafs.size() < K) {
        // std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        // std::cout << "Leafs: " << leafs.size() << " | Treecost: " << leaf_cost_total << std::endl;

        auto r = fdistr(rng) * leaf_cost_total;
        float acc = 0.0;
        int_t leaf_idx = 0;
        while (leaf_idx < leafs.size()) {
            acc += leafs[leaf_idx].cost;
            if (acc >= r) break;
        }

        std::cout << "Picking leaf idx = " << leaf_idx << " with cost = "  << leafs[leaf_idx].cost << ", total cost = " << leaf_cost_total 
                    << ", cidx = " << leafs[leaf_idx].centroid_idx << std::endl;

        auto& leaf = leafs[leaf_idx];
        const auto L_N = leaf.end - leaf.start + 1;

        
        // 2. Pick the farthest point from the centroid of the leaf
        int_t point_idx = 0;
        float minC1 = leaf.cost, minC2 = leaf.cost;
        std::vector<int_t> new_points_idx = std::vector<int_t>();


        for (int _rr = 0; _rr < 100; ++_rr) {
            float c2 = 0.0, c1 = 0.0;
        
            // print some deboub info like cost of the leaf, minCostPoint, MaxCostPoint, avg, and var2
            // std::cout << "Leaf[" << leaf.centroid_idx << "]: Cost = " << leaf.cost << ", Points = " << L_N; 
            // float minCostPoint = std::numeric_limits<float>::max();
            // float maxCostPoint = std::numeric_limits<float>::min();
            // float avgCostPoint = 0.0;
            // float var2CostPoint = 0.0;
            // for (int_t i = leaf.start; i <= leaf.end; ++i) {
            //     const float cost = distances[i];
            //     if (cost < minCostPoint) {
            //         minCostPoint = cost;
            //     }
            //     if (cost > maxCostPoint) {
            //         maxCostPoint = cost;
            //     }
            //     avgCostPoint += cost;
            // }
            // avgCostPoint /= L_N;
            // for (int_t i = leaf.start; i <= leaf.end; ++i) {
            //     const float cost = distances[i];
            //     var2CostPoint += (cost - avgCostPoint) * (cost - avgCostPoint);
            // }
            // var2CostPoint /= L_N;
            // std::cout << "\n\tMinCostPoint = " << minCostPoint 
            //           << ", MaxCostPoint = " << maxCostPoint 
            //           << ", AvgCostPoint = " << avgCostPoint 
            //           << ", Var2CostPoint = " << var2CostPoint 
            //           << ", STD = " << std::sqrt(var2CostPoint)
            //           << std::endl;

            int_t pidx = ({
                    float r = fdistr(rng) * leaf.cost;
                    // std::cout << "Picking point with r = " << r << " | ";
                    float cost_sum = 0.0;
                    int_t pidx = 0;

                    for (int_t i = leaf.start; i <= leaf.end; ++i) {
                        cost_sum += distances[i];
                        if (cost_sum >= r) {
                            pidx = i;
                            break;
                        }
                    }
                    // std::cout << "Picked point idx = " << pidx << " with cost = " << distances[pidx] << std::endl;
                    pidx;
                });


            // std::cout << "Leaf[" << leaf.centroid_idx << "]: Picking point idx = " << pidx <<  " cost = " << distances[pidx] << " | ";
            // for (int_t i = 0; i < std::min(static_cast<int_t>(10),  D); ++i) {
            //     std::cout << points[pidx * D + i] << " ";
            // }

            // if (D > 10) {
            //     std::cout << "... ";
            // }

            // std::cout << std::endl << std::endl;

            // 3. Compute the new distances based on the min(distances, new_distance)
            // const float* const __restrict__ new_centroid = &points[pidx * D];
            float * new_centroid = &points[pidx * D];

            std::vector<int_t> swp_idx;
            swp_idx.reserve(L_N);

            for (int_t i = leaf.start; i <= leaf.end; ++i) {
                float new_distance = 0.0;

                for (int_t j = 0; j < D; ++j) {
                    const float diff = points[i * D + j] - new_centroid[j];
                    new_distance += diff * diff;
                }


                if (new_distance < distances[i]) {
                    swp_idx.push_back(i);
                    c2 += new_distance;
                } else {
                    c1 += distances[i];
                }
                
            }

            // std::cout << "curr min: " << (minC1 + minC2) << ", new: " << (c1 + c2) << std::endl;

            if (c1 + c2 < minC1 + minC2) {
                assert(swp_idx.size() > 0 && "Swp index should not be empty, how can cost be lower and no points move?");

                minC1 = c1;
                minC2 = c2;
                point_idx = pidx;
                new_points_idx = std::move(swp_idx);
            } 
        }

        fassert(minC1 + minC2 < leaf.cost, "New cost should be lower than the current cost");

        // udpate distances (loop through new_points_idx)
        for (const auto& idx : new_points_idx) {
            float new_distance = 0.0;
            for (int_t j = 0; j < D; ++j) {
                const float diff = points[idx * D + j] - points[point_idx * D + j];
                new_distance += diff * diff;
            }
            distances[idx] = new_distance;
        }


        std::cout << "Coreset: New point idx = " << point_idx
                  << ", c1 = " << minC1 << ", c2 = " << minC2 
                  << ", new_points_idx.size() = " << new_points_idx.size() 
                  << std::endl;


        const int_t s1 = leaf.start, e1 = leaf.end - new_points_idx.size(), s2 = e1 + 1, e2 = leaf.end;
        int_t cur = e2;
        int_t i = new_points_idx.size() - 1;
        while (i != std::numeric_limits<int_t>::max()) {
            int_t idx = new_points_idx[i];
            i--;

            for (int_t j = 0; j < D; ++j) {
                std::swap(points[cur * D + j], points[idx * D + j]);
            }

            std::swap(distances[cur], distances[idx]);
            cur--;
        }


        leaf_cost_total -= leaf.cost;
        leaf_cost_total += minC1 + minC2;

        // UPD PRV LEAF 
        leaf.start = s1;
        leaf.end = e1;
        leaf.cost = minC1;
        leafs.emplace_back(leaf_t{point_idx, s2, e2, minC2});



        // print all ranges of leafs (debugging)
        // if (leafs.size() < 10) {
            std::cout << "Leafs: " << leafs.size() << " | ";
            // if (leafs.size() < 10) {
                for (const auto& l : leafs) {
                    // std::cout << "[" << l.start << ", " << l.end << "] ";
                    std::cout << l.end - l.start + 1 << "(" << l.cost << ") ";
                }
            // }
          
            std::cout << std::endl;
        // }
    }

    delete[] __distances;
}

int main(int argc, char** argv) {
    auto perf = PerfManager();
    perf.pause(); 
    auto [samples, outDir, coreset_size] = parseArgs<float>(argc, argv);

    if (coreset_size == 0) {
        coreset_size = samples.samples / 2;
    }

    perf.resume();
    Coreset(samples.data.data(), static_cast<size_t>(600), samples.features, coreset_size);
    perf.pause();


    return 0;
}