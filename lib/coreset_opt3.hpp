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

template<bool WithWeight = false>
inline double distance(std::span<float> p1, std::span<float> p2) {
    dassert(p1.size() == p2.size(), "Points size mismatch");
    double dist = 0.0;
    
    if constexpr (WithWeight) {
        double w1 = static_cast<double>(p1[0]);
        double w2 = static_cast<double>(p2[0]);

        for (uint64_t d = 1; d < p1.size(); ++d) {
            // double diff = static_cast<double>(p1[d]) / w1 - static_cast<double>(p2[d]) / w2;
            // dist += (diff * diff);
            double diff = static_cast<double>(p1[d]) - static_cast<double>(p2[d]);
            dist += (diff * diff);
        }

        // dist *= w2;
    } else {
        for (uint64_t d = 0; d < p1.size(); ++d) {
            double diff = static_cast<double>(p1[d]) - static_cast<double>(p2[d]);
            dist += diff * diff;
        }
    }

    return dist;
};


template<typename int_t = std::size_t, bool WithWeight = false, int MinSplitIters=3, unsigned int Seed = 0U>
std::vector<float> Coreset(float * const __restrict__ __points, const int_t N, const int_t D, const int_t K) {

    struct Node {
        int_t center_idx;
        std::vector<int_t> points;
        Node *lc, *rc, *parent;
        std::span<double> distances;
        float cost;

        Node() : cost(0.0), parent(nullptr), rc(nullptr), lc(nullptr), center_idx(0), distances(std::span<double>()) {}
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

        if constexpr (WithWeight) {
            return std::span<float>((__points + idx * (D + 1)), D + 1);
        } else {
            return std::span<float>(__points + idx * D, D);
        }
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
            auto dist = distance<WithWeight>(center, get_point(idx));
            curr_distance[idx] = dist;
            cost += dist;
        }

        root.cost = cost;
        root.distances = std::span<double>(curr_distance.data(), N);
    }


    int_t iterations = 1;


    struct compute_tmp {
        double cost;
        bool new_center;
    };

    while (iterations < K) {

        Node* leaf = &root;
        while(leaf->lc) {
            Node* L = leaf->lc;
            Node* R = leaf->rc;

            double cL = L->cost;
            double cR = R->cost;
        
            if (cL == 0.0f) {
                leaf = R;
                continue;
            }
           
            if (cR == 0.0f) {
                leaf = L;
                continue;
            }

            double p = random_double(gen) * leaf->cost;
            leaf = (p < cL) ? L : R;
        }   

        const int_t LeaftPointsSize = leaf->points.size();

        dassert(LeaftPointsSize >= 2, "Cannot split a leaf with < 2 points");

        int_t SplitIters = std::min(static_cast<size_t>(MinSplitIters), LeaftPointsSize - 1);

        auto split_centers = std::array<int_t, MinSplitIters>();
        auto probs = std::array<double, MinSplitIters>();
        for (int_t i = 0; i < SplitIters; ++i) {
            probs[i] = random_double(gen) * leaf->cost; 
        }
        std::sort(probs.begin(), probs.end());

        int_t p_index = 0;
        double acc = 0.0;

        for (int_t i = 0; i < LeaftPointsSize; ++i) {
            acc += leaf->distances[i];

            if (acc >= probs[p_index]) {
                int_t idx = leaf->points[i];
                if (idx == leaf->center_idx) continue; 
                
                split_centers[p_index] = idx;
                p_index++;

                if (p_index >= SplitIters) break; // Found enough centers
            }
        }

    

        int_t min_center = leaf->center_idx;
        int_t min_center_n_points = 0;
        double min_cost = leaf->cost;

        auto compute_min = std::vector<compute_tmp>(LeaftPointsSize);
        auto compute = std::vector<compute_tmp>(LeaftPointsSize);

        for (int_t center_idx = 0; center_idx < SplitIters; ++center_idx) {
            int_t new_center = split_centers[center_idx];

            double curr_min_cost = 0.0;
            int_t new_center_n_points = 0;

            auto span_old_center = get_point(leaf->center_idx);
            auto span_new_center = get_point(new_center);

            for (int_t i = 0; i < LeaftPointsSize; ++i) {
                int_t pidx = leaf->points[i];

                double cost_old = leaf->distances[i];
                double cost_new = distance<WithWeight>(span_new_center, get_point(pidx));
                
                double min_cost = std::min(cost_old, cost_new);
                compute[i] = {min_cost, cost_old >= cost_new};
                curr_min_cost += min_cost;
                new_center_n_points += cost_old < cost_new ? 0 : 1;
            }

            if (curr_min_cost < min_cost) {
                std::swap(compute_min, compute);
                min_cost = curr_min_cost;
                min_center = new_center;
                min_center_n_points = new_center_n_points;
            }
        }

        dassert(min_cost < leaf->cost, "New cost must be lower then current cost");
        dassert(min_center != leaf->center_idx, "New center must be different from current one!");


        // Recalcute indices and costs 

        std::vector<int_t> indices_old = std::vector<int_t>(LeaftPointsSize - min_center_n_points);
        std::vector<int_t> indices_new = std::vector<int_t>(min_center_n_points);
       
        // std::span<double> old_dist_span(leaf->distances.data(), LeaftPointsSize - min_center_n_points);
        std::span<double> old_dist_span = leaf->distances.subspan(0, LeaftPointsSize - min_center_n_points);
        std::span<double> new_dist_span = leaf->distances.subspan(LeaftPointsSize - min_center_n_points, min_center_n_points);

        auto span_old_center = get_point(leaf->center_idx);
        auto span_new_center = get_point(min_center);

        double total_old_cost = 0.0;
        double total_new_cost = 0.0;


        int_t off_indices_old = 0;
        int_t off_indices_new = 0;

        for (int_t i = 0; i < LeaftPointsSize; ++i) {
            int_t pidx = leaf->points[i];

            double cost = compute_min[i].cost;

            if(compute_min[i].new_center) {
                total_new_cost += cost;
                new_dist_span[off_indices_new] = cost; // Update distance for new center
                indices_new[off_indices_new++] = pidx; // Use preallocated space
            } else {
                total_old_cost += cost;
                old_dist_span[off_indices_old] = cost; // Update distance for old center
                indices_old[off_indices_old++] = pidx; // Use preallocated space
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
        lc->distances = std::move(old_dist_span);
        
        rc->center_idx = min_center;
        rc->points = std::move(indices_new);
        rc->cost = total_new_cost;
        rc->parent = leaf;
        rc->distances = std::move(new_dist_span);

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

    std::vector<float> coreset (K * (D + 1));
    int_t offs = 0;
  
    for (Node& node : node_arena) {
        if (node.lc) continue; 
        auto point = get_point(node.center_idx);
        coreset[offs++] = static_cast<float>(node.points.size());

        fassert(offs <= coreset.size(), "Offset out of bounds");

        if constexpr (WithWeight) {
            fassert(point.size() == D + 1, "Point size mismatch");

            for (int_t d = 1; d < (D + 1); ++d)
                coreset[offs++] = point[d];
        } else {

            for (float d : point) 
                coreset[offs++] = d;
        }
    }

    fcontext("with weight: " + std::to_string(WithWeight));
    fassert(offs == coreset.size(), "Offset should be equal to coreset size, got " + std::to_string(offs) + " vs " + std::to_string(coreset.size()));

    return coreset;

}
