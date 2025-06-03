#pragma once

#include <cstddef>
#include <cmath>
#include <stdexcept>
#include <cassert>
#include <variant>
#include <vector>
#include <iostream>
#include <cstring>


// cretate a fale _cout 
class fake_ostream {
public:
    template<typename T>
    fake_ostream& operator<<(const T&) {
        return *this;
    }

    fake_ostream& operator<<(std::ostream& (*)(std::ostream&)) {
        return *this;
    }
};


#ifdef DEBUG
#define _cout std::cout
#else
#define _cout fake_ostream()
#endif



inline float __rand_float() {
    return rand() / (RAND_MAX + 1.0);
}

inline size_t __pick_random(size_t n) {
    return rand() % n;
}

struct flat_points {
    float* points;
    size_t n, d;

    flat_points() : points(nullptr), n(0), d(0) {}

    flat_points(float* points, size_t n, size_t d) : points(points), n(n), d(d) {}

    float& operator()(size_t i, size_t j) {
        return points[i * d + j];
    }

    const float& operator()(size_t i, size_t j) const {
        return points[i * d + j];
    }

    float* operator[](size_t i) {
        return points + i * d;
    }

    const float* operator[](size_t i) const {
        return points + i * d;
    }
};

typedef struct indices {
    size_t* indices_vec;
    size_t* indices_vec_tmp;
    size_t n;
    size_t start;

    static indices create(size_t n) {
        size_t* indices_vec = new size_t[n];
        size_t* indices_vec_tmp = new size_t[n];
        for (size_t i = 0; i < n; ++i) {
            indices_vec[i] = i;
        }

        return indices(n, indices_vec, indices_vec_tmp);
    }

    std::pair<indices, indices> split(
        const size_t c1IDX,
        const size_t c2IDX,
        const struct flat_points points) {
            
     

        
        size_t cur0 = start;
        size_t cur1 = start + n - 1;
        
        assert(cur0 <= cur1 && "This should never happen");
        _cout << "CoresetTree: split: cur0 = " << cur0 << ", cur1 = " << cur1 << " delta = " << cur1 - cur0 << "\n";

        // const float* c1 = points[c1IDX];
        // const float* c2 = points[c2IDX];

        float* c1 = (float *) alloca(points.d * sizeof(float));
        float* c2 = (float *) alloca(points.d * sizeof(float));
        for (size_t j = 0; j < points.d; ++j) {
            c1[j] = points[c1IDX][j];
            c2[j] = points[c2IDX][j];
        }

        int i = start;
        bool last_is_c0 = false;

        const size_t d = points.d;
        while (cur0 <= cur1) {
            const size_t idx = indices_vec[i];
            const float* p = points[idx];

            float dist1 = 0.0;
            float dist2 = 0.0;

            for (size_t j = 0; j < d; ++j) {
                dist1 += (c1[j] - p[j]) * (c1[j] - p[j]);
                dist2 += (c2[j] - p[j]) * (c2[j] - p[j]);
            }

            if (dist1 < dist2) {
                indices_vec_tmp[cur0] = indices_vec[i];
                last_is_c0 = true;
                cur0++;
            } else {
                indices_vec_tmp[cur1] = indices_vec[i];
                last_is_c0 = false;
                cur1--;
            }

            i++;
        }

        size_t *swap = indices_vec;
        indices_vec = indices_vec_tmp;
        indices_vec_tmp = swap;

        assert(cur0 > cur1);

        cur0 = last_is_c0 ? cur0 - 1 : cur0 - 1;
        cur1 = last_is_c0 ? cur1 + 1 : cur1 + 1;

        _cout << "CoresetTree: split: cur0 = " << cur0 << ", cur1 = " << cur1 << "\n";
        assert(cur1 > cur0);


        // Full range is [start, n - 1]
        // Split 1 goes to [start, cur0]
        // Split 2 goes to [cur1, n - 1]

        size_t n0 = cur0 - start + 1;
        size_t n1 = (start + n) - cur0 - 1;

        size_t start0 = start;
        size_t start1 = cur1;

        // _cout << "CoresetTree: split: left [" << start0 << ", " << start0 + n0 << "] right [" << start1 << ", " << start1 + n1 << "]\n";
        _cout << "CoresetTree: split: left [" << start0 << ", " << start0 + n0 - 1 << "] (" << n0 << ") right [" << start1 << ", " << start1 + n1 - 1 << "] (" << n1 << ") sum: " << n0 + n1 << "\n";
        assert(n0 + n1 == n && "Sum of split sizes should be equal to n");

        return {
            indices(n0, indices_vec, indices_vec_tmp, start0),  
            indices(n1, indices_vec, indices_vec_tmp, start1)   
        };

    }

    size_t size() const {
        return n;
    }

    size_t& operator[](size_t i) {
        return indices_vec[i + start];
    }

    const size_t& operator[](size_t i) const {
        return indices_vec[i + start];
    }

private:
    indices(size_t n, size_t* indices_vec, size_t* indices_vec_tmp, size_t start = 0)
        : n(n), indices_vec(indices_vec), indices_vec_tmp(indices_vec_tmp), start(start) {}


} CTIndices;

typedef struct coreset {
    flat_points points;
    float *weights;

    coreset() : points(), weights(nullptr) {}

    coreset(size_t n, size_t d) : points(nullptr, n, d), weights(nullptr) {
        points.points = new float[n * d];
        weights = new float[n];
    }

    coreset(const coreset&) = delete;
    coreset& operator=(const coreset&) = delete;

    coreset(coreset&& other) noexcept : points(other.points), weights(other.weights) {
        weights = other.weights;
        points = other.points;

        other.weights = nullptr;
        other.points = flat_points();
    }

    coreset& operator=(coreset&& other) noexcept {
        if (this != &other) {
            weights = other.weights;
            points = other.points;

            other.weights = nullptr;
            other.points = flat_points();
        }
        return *this;
    }

   

    ~coreset() {
        if (points.points != nullptr) {
            delete[] points.points;
        }
        if (weights != nullptr) {
            delete[] weights;
        }
    }

    std::string csv() {
        std::string csv = "w";
        for (size_t j = 0; j < points.d; ++j) {
            csv += ",x" + std::to_string(j);
        }
        csv += "\n";

        for (size_t i = 0; i < points.n; ++i) {
            csv += std::to_string(weights[i]);
            for (size_t j = 0; j < points.d; ++j) {
                csv += "," + std::to_string(points[i][j]);
            }
            csv += "\n";
        }

        return csv;
    }

} Coreset;

#include <immintrin.h>


static inline float hsum256_ps(__m256 v) {
    __m128 vlow  = _mm256_castps256_ps128(v);
    __m128 vhigh = _mm256_extractf128_ps(v, 1);
    vlow  = _mm_add_ps(vlow, vhigh);
    __m128 shuf = _mm_movehdup_ps(vlow);
    __m128 sums = _mm_add_ps(vlow, shuf);
    shuf        = _mm_movehl_ps(shuf, sums);
    sums        = _mm_add_ss(sums, shuf);
    return _mm_cvtss_f32(sums);
}

typedef struct ct_node {
    struct flat_points points; // NON OWNING PROPS
    float* weights; // NON OWNING PROPS
    
    ct_node *parent, *lc, *rc;
    size_t centerIDX;
    CTIndices indices;

    float cost;

    bool is_leaf() const {
        assert((lc == nullptr) == (rc == nullptr));
        return lc == nullptr && rc == nullptr;
    }

    ct_node& random_leaf() {
        if (is_leaf()) {
            return *this;
        } else {
            float r = __rand_float();
            float left_prob = lc->cost / cost;
            if (r < left_prob) {
                return lc->random_leaf();
            } else {
                return rc->random_leaf();
            }
        }
    }

    template<int Rounds=1>
    size_t pick_centroid() {
        assert(!std::isnan(cost) && !std::isinf(cost) && "Cost should not be NaN or Inf");

        float min_cost = cost; // The new centroid cost should be less than the current cost 

        size_t pickedIDX = 0;
        const float * centre = points[centerIDX];
        const float centre_weight = weights != nullptr ? weights[centerIDX] : 1.0;
        const size_t d = points.d;
        for (int r = 0; r < Rounds; ++r) {
            _cout << "CoresetTree: pick_centroid: round " << r << "\n";

            float sum = 0.0;
            float rand = __rand_float();

            // loop for all points (loop through all indices)
            for (size_t i = 0; i < indices.size(); ++i) {
                const size_t idx = indices[i];
                const float* p = points[idx];
                const float pweight =  weights != nullptr ? weights[idx] : 1.0;


                double priv_sum = 0.0;
                for (size_t j = 0; j < d; ++j) {
                    const float diff = (centre[j]/centre_weight - p[j]/pweight);
                    priv_sum += diff * diff;
                }

                sum += priv_sum / cost;


                assert(!std::isnan(sum) && !std::isinf(sum) && "Sum should not be NaN or Inf");

                if (sum >= rand) {

                    float split_cost = 0.0;

                    // #pragma omp parallel for reduction(+:split_cost)
                    for (size_t k = 0; k < indices.size(); ++k) {
                        const size_t idx2 = indices[k];
                        const float* p2 = points[idx2];
                        const float p2weight = weights != nullptr ? weights[idx2] : 1.0;

                        float dist1 = 0.0;
                        float dist2 = 0.0;

                        for (size_t j = 0; j < d; ++j) {
                            const float diff1 = (centre[j]/centre_weight - p2[j]/p2weight);
                            dist1 += diff1 * diff1;
                            const float diff2 = (p[j]/pweight - p2[j]/p2weight);
                            dist2 += diff2 * dist2;
                        }

                        split_cost += dist1 < dist2 ? dist1 : dist2;
                    }

                    if (split_cost < min_cost) {
                        min_cost = split_cost;
                        pickedIDX = idx;
                        break; // We found a point that is better than the current center
                    }

                }
            }
        }

        _cout << "CoresetTree: pick_centroid: pickedIDX = " << pickedIDX << ", min_cost = " << min_cost <<  ", cost = " << cost << "\n";
        assert(min_cost < cost);
        return pickedIDX;
    }

    void split(size_t sampleIDX) {
        auto [left_indices, right_indices] = indices.split(centerIDX, sampleIDX, points);
        _cout << "CoresetTree: split: left size = " << left_indices.size() << ", right size = " << right_indices.size() << "\n";

        lc = new ct_node(points, left_indices, centerIDX, this);
        rc = new ct_node(points, right_indices, sampleIDX, this);

        ct_node* node = this;
        while (node != nullptr) {
            node->cost = node->lc->cost + node->rc->cost;
            node = node->parent;
        }
    }

    static ct_node root(const flat_points& points, float* weights) {
        CTIndices indices = CTIndices::create(points.n);
        return ct_node(points, indices, indices[__pick_random(indices.size())], nullptr, weights);
    }

    float calcCost() {
        const size_t d = points.d;
        const float* center = points[centerIDX];
        const float center_weight = weights != nullptr ? weights[centerIDX] : 1.0;

        float sum = 0.0;
        for (size_t i = 0; i < indices.size(); ++i) {
            const size_t idx = indices[i];
            const float* p = points[idx];
            const float pweight = weights != nullptr ? weights[idx] : 1.0;

            for (size_t j = 0; j < d; ++j) {
                const float diff = (center[j]/center_weight - p[j]/pweight);
                assert(!std::isnan(diff) && !std::isinf(diff) && "Diff should not be NaN or Inf");
                sum += diff * diff;
            }
        }

        assert(!std::isnan(sum) && !std::isinf(sum) && "Sum should not be NaN or Inf");
        return sum;
    }

    ~ct_node() {
        if (parent == nullptr) {
            delete[] indices.indices_vec;
            delete[] indices.indices_vec_tmp;
        }
        
        delete lc;
        delete rc;
    }

    void printTree() {
        auto queue = std::vector<std::pair<ct_node*, int>>();
        queue.push_back({this, 0});

        while (!queue.empty()) {
            auto [node, level] = queue.back();
            queue.pop_back();
            for (int i = 0; i < level; ++i) {
                _cout << " | ";
            }
            // _cout << "CoresetTree: level = " << level << ", centerIDX = " << node->centerIDX << ", cost = " << node->cost << ", size = " << node->indices.size() << "\n";
            _cout << "[" << level << "] cost = " << node->cost << ", size = " << node->indices.size() << "\n";

            if (node->lc != nullptr) {
                queue.push_back({node->lc, level + 1});
            }
            if (node->rc != nullptr) {
                queue.push_back({node->rc, level + 1});
            }
        }
      
    }

    size_t count_leafs() {
        if (is_leaf()) {
            return 1;
        } else {
            return lc->count_leafs() + rc->count_leafs();
        }
    }

    template <typename Func>
    void for_each_leaf(Func&& f) const {
        if (is_leaf()) {
            f(this);
        } else {
            if (lc) lc->for_each_leaf(f);
            if (rc) rc->for_each_leaf(f);
        }
    }

    Coreset extract_coreset() {
        size_t m = count_leafs();

        Coreset coreset(m, points.d);
        const size_t d = points.d;
        size_t idx = 0;


        std::vector<ct_node*> nodes = {this};

        while (!nodes.empty()) {
            ct_node* node = nodes.back();
            nodes.pop_back();

            if (node->is_leaf()) {
                const size_t centerIDX = node->centerIDX;
                const float* center = node->points[centerIDX];
                const float new_weight = node->indices.size();

                coreset.weights[idx] = new_weight;
                for (size_t j = 0; j < d; ++j) {
                    coreset.points[idx][j] = center[j];
                }

                idx++;
            } else {
                if (node->lc) nodes.push_back(node->lc);
                if (node->rc) nodes.push_back(node->rc);
            }
        }


        // for_each_leaf([&coreset, &idx, d](const ct_node* node) {
        //     const size_t centerIDX = node->centerIDX;
        //     const float* center = node->points[centerIDX];
        //     const float new_weight = node->indices.size();

        //     coreset.weights[idx] = new_weight;
        //     for (size_t j = 0; j < d; ++j) {
        //         coreset.points[idx][j] = center[j];
        //     }

        //     idx++;
        // });
        
        assert (coreset.points.n == m && "Coreset size should be equal to number of leafs");

        return coreset;
    }

private:
    ct_node(const flat_points& points, const CTIndices& indices, size_t centerIDX, ct_node* parent = nullptr, float* weights = nullptr)
        : points(points), indices(indices), parent(parent), lc(nullptr), rc(nullptr), centerIDX(centerIDX), cost(0.0), weights(weights) {
            cost = calcCost();
    }

} CTNode;


CTNode CoresetTree(float* points, float* weights, size_t n, size_t d, size_t k) {

    _cout << "CoresetTree: n = " << n << ", d = " << d << ", k = " << k << "\n";
    CTNode root = CTNode::root(flat_points(points, n, d), weights);

    _cout << "CoresetTree: root center = " << root.centerIDX << "\n";
    _cout << "CoresetTree: root cost = " << root.cost << "\n";
    _cout << "CoresetTree: root size = " << root.indices.size() << "\n";

    _cout << std::endl;
    for (size_t i = 1; i < k; ++i) {
        _cout << "[CoresetTree] Iteration " << i << "\n";

        CTNode& leaf = root.random_leaf();

        _cout << "CoresetTree: leaf center = " << leaf.centerIDX << "\n";
        _cout << "CoresetTree: leaf cost = " << leaf.cost << "\n";
        _cout << "CoresetTree: leaf size = " << leaf.indices.size() << "\n";

        size_t pickedIDX = leaf.pick_centroid();

        _cout << "CoresetTree: picked center = " << pickedIDX << "\n";

        leaf.split(pickedIDX);
        _cout << "CoresetTree: leaf cost after split = " << leaf.cost << "\n";
        _cout << "CoresetTree: leaf size after split = " << leaf.indices.size() << "\n";
    }

    return root;
}

