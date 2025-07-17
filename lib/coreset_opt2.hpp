#include <iostream>
#include <random>

#include <vector>
#include <variant>
#include <tuple>
#include <span>
#include <set>
#include <functional>

#include <chrono>
#include "assert.hpp"

struct xorshift128p_state {
    uint64_t a, b;

    // Seed must be non-zero
    xorshift128p_state(uint64_t seed) : a(seed), b(seed) {}

    uint64_t operator()() {
        uint64_t t = a;
        const uint64_t s = b;
        a = s;
        t ^= t << 23;       // a
        t ^= t >> 17;       // b
        t ^= s;             // c
        t ^= s >> 26;       // d
        b = t;
        return t + s;
    }
};

// Helper to generate a double in [0, 1)
inline double random_double(xorshift128p_state& gen) {
    // Use the top 53 bits for a double-precision float
    return (gen() >> 11) * 0x1.0p-53;
}

inline double distance(std::span<float> p1, std::span<float> p2) {
    dassert(p1.size() == p2.size(), "Points size mismatch");
    
    double dist = 0.0;
    for (uint64_t d = 0; d < p1.size(); ++d) {
        double diff = static_cast<double>(p1[d]) - static_cast<double>(p2[d]);
        dist += diff * diff;
    }

    return dist;
};


template<typename int_t = std::size_t, int MinSplitIters=3, unsigned int Seed = 0U>
std::vector<float> Coreset(float * const __restrict__ __points,  const int_t N, const int_t D, const int_t K) {

    // std::cout << "Coreset for N: " << N << " D: " << D << "K: " << K << std::endl;

    std::vector<int_t> coreset_indexes(K);

    struct Node {
        int_t center_idx;
        std::vector<int_t> points;
        Node *lc, *rc, *parent;
        std::span<double> distances;
        float cost;

        Node() : cost(0.0), parent(nullptr), rc(nullptr), lc(nullptr), center_idx(0), distances(std::span<double>()) {
        }
    };

    std::vector node_arena = std::vector<Node>(0);
    node_arena.reserve(K * 2); // Reserve space for K nodes and their children

    std::random_device rd;
    
    unsigned int seed_value;
    if constexpr (Seed == 0U) {
        seed_value = rd();
    } else { 
        seed_value = Seed;
    }

    dcontext("Seed: " + std::to_string(seed_value));
    xorshift128p_state gen(seed_value);

    // std::uniform_real_distribution<> prob_distr(0.0, 1.0);

    auto get_point = [&__points, N, D](int_t idx) {
        dassert(idx >= 0 && idx < N, "index out of bound");
        return std::span<float>(__points + idx * D, D);
    };

    auto curr_distance = std::vector<double>(N, 0.0);
    Node root = Node();
    { // Root initialization
        
        std::mt19937 gen(seed_value);
        std::uniform_int_distribution<int_t> dis(0, N - 1);
        int_t random_idx = dis(gen);

        auto indices = std::vector<int_t>(N);
        for (int i = 0; i < N; ++i) indices[i] = i;

        root.center_idx = random_idx;
        root.points = std::move(indices);

        auto center = get_point(random_idx);
        double cost = 0.0;
        for (int_t idx : root.points) {
            auto dist = distance(center, get_point(idx));
            curr_distance[idx] = dist;
            cost += dist;
        }

        root.cost = cost;
        root.distances = std::span<double>(curr_distance.data(), N);
        root.distances = std::span<double>(curr_distance.data(), N);
        coreset_indexes[0] = random_idx;
    }


    int_t iterations = 1;

    while (iterations < K) {

        // std::cout << "\r\e[2K" << "Iteration: " << iterations + 1 << "/" << K << std::flush;
        // std::cout << std::endl;
        // std::cout << "Root cost: " << root.cost << std::endl;


        Node* leaf = &root;
        while(leaf->lc) {
            if (leaf->lc->cost == 0) {
                leaf = leaf->rc;
            } else if (leaf->rc->cost == 0) {
                leaf = leaf->lc;
            } else {
                auto p = random_double(gen);
                if (p < leaf->lc->cost / leaf->cost) 
                    leaf = leaf->lc;
                else 
                    leaf = leaf->rc;
            }
        }

        dassert(leaf->points.size() >= 2, "Cannot split a leaf with < 2 points");


        int_t SplitIters = std::min(static_cast<size_t>(MinSplitIters), leaf->points.size() - 1);

        // std::cout << "Choosen leaf! now lets find " << SplitIters << " Centers out of " << leaf->points.size() << " points" << std::endl;

        std::set<int_t> split_centers = std::set<int_t>();


        // if (leaf->points.size() < 20) {
        //     std::cout << "Points: ";
        //     for (int_t idx : leaf->points) {
        //         double dist =  distance(get_point(leaf->center_idx), get_point(idx));
        //         std::cout << idx << " " << dist << ", " ;
        //     }
    
        //     std::cout << std::endl;
        // }
       

        for (int_t iter = 0; split_centers.size() < SplitIters && iter < SplitIters * 4; ++iter) {
            int_t choosen_point = leaf->center_idx;
            auto p = random_double(gen) * leaf->cost;

            double acc = 0.0;
            auto center = get_point(leaf->center_idx);


            for (int_t idx : leaf->points) {

                if (idx == leaf->center_idx) 
                    continue;

                acc += curr_distance[idx];
                
                if (acc >= p) {
                    choosen_point = idx;
                    break;
                }
            }

            // std::cout << "Iter " << iter <<  " Choosen point with prob: " << p << " point: " << choosen_point << std::endl;


            dassert(choosen_point != leaf->center_idx, "Choosen point to split should not be the current center");
            split_centers.insert(choosen_point);
        } 

        dassert(split_centers.size() > 0, 
            "Max iter retryis reached while finding " + std::to_string(SplitIters) 
                + " centers to split from. (leaf costs: " + std::to_string(leaf->cost) + ", leaf points: " + std::to_string(leaf->points.size()) + ")");

        // std::cout << "choosen the centers to try" << std::endl;


        int_t min_center = leaf->center_idx;
        double min_cost = leaf->cost;

        for (int_t new_center : split_centers) {

            double curr_min_cost = 0.0;

            auto span_old_center = get_point(leaf->center_idx);
            auto span_new_center = get_point(new_center);

            for(int_t pidx : leaf->points) {
                double cost_old = curr_distance[pidx];
                // double cost_old = distance(span_old_center, get_point(pidx));
                double cost_new = distance(span_new_center, get_point(pidx));
                curr_min_cost += cost_old < cost_new ? cost_old : cost_new;
            }

            if (curr_min_cost < min_cost) {
                min_cost = curr_min_cost;
                min_center = new_center;
            }
        }

        dassert(min_cost < leaf->cost, "New cost must be lower then current cost");
        dassert(min_center != leaf->center_idx, "New center must be different from current one!");


        coreset_indexes[iterations] = min_center;

        // Recalcute indices and costs 

        std::vector<int_t> indices_old;
        std::vector<int_t> indices_new;

        auto span_old_center = get_point(leaf->center_idx);
        
        auto span_new_center = get_point(min_center);

        double total_old_cost = 0.0;
        double total_new_cost = 0.0;

        for (int_t pidx : leaf->points) {
            double cost_old = curr_distance[pidx];
            double cost_new = distance(span_new_center, get_point(pidx));

            if (cost_old < cost_new) {
                total_old_cost += cost_old;
                indices_old.push_back(pidx);
                curr_distance[pidx] = cost_old; // Update distance for old center
            } else {
                total_new_cost += cost_new;
                indices_new.push_back(pidx);
                curr_distance[pidx] = cost_new; // Update distance for new center
            }
        }



        // Create and initialize new leafs

        node_arena.resize(node_arena.size() + 2);
        Node *lc = &node_arena[node_arena.size() - 2];
        Node *rc = &node_arena[node_arena.size() - 1];

        lc->center_idx = leaf->center_idx;
        lc->points = std::move(indices_old);
        lc->cost = total_old_cost;
        lc->parent = leaf;
        
        rc->center_idx = min_center;
        rc->points = std::move(indices_new);
        rc->cost = total_new_cost;
        rc->parent = leaf;

        leaf->rc = rc;
        leaf->lc = lc;

        // update recusively parents of the current leaf
        while (leaf) {
            leaf->cost = leaf->rc->cost + leaf->lc->cost;
            leaf = leaf->parent;
        }

        iterations++;
    }
    // std::cout << std::endl;


    std::vector<float> coreset (K * D);
    int_t offs = 0;
    for (int_t idx : coreset_indexes) {
        auto point = get_point(idx);
        for ( float d : point) 
            coreset[offs++] = d;
    }

    return coreset;
}
