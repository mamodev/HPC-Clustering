#pragma once

#include <cstddef>
#include <cmath>
#include <stdexcept>
#include <variant>
#include <vector>
#include <iostream>
#include <cstring>
#include <source_location>
#include <stacktrace>

void assert(bool condition, std::string message = "Assertion failed",
            const std::source_location& location = std::source_location::current()) {
    if (!condition) {
        std::cerr << "[Assertion failed] " << message 
                  << " at " << location.file_name() 
                  << ":" << location.line() 
                  << " in function " << location.function_name() << std::endl;
        
        std::cout << std::stacktrace::current() << '\n';

        
        throw std::runtime_error(message);
        }
}

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
    const size_t n; // number of points
    const size_t d; // dimension of points

    flat_points() : points(nullptr), n(0), d(0) {}

    flat_points(float* points, size_t n, size_t d) : points(points), n(n), d(d) {}

    float& operator()(size_t i, size_t j) {
        assert(points != nullptr, "Points should not be null");
        assert(i < n, "Index i out of bounds");
        assert(j < d, "Index j out of bounds");
        assert(d > 0, "Dimension d should be greater than 0");
        assert(points + i * d < points + n * d,  "1) Accessing out of bounds in flat_points");
        assert(points + i * d + d <= points + n * d,  "2) Accessing out of bounds in flat_points");
        return points[i * d + j];
    }

    const float& operator()(size_t i, size_t j) const {
        assert(points != nullptr,  "Points should not be null");
        assert(i < n,  "Index i out of bounds");
        assert(j < d,  "Index j out of bounds");
        assert(d > 0,  "Dimension d should be greater than 0");
        assert(points + i * d < points + n * d,  "1) Accessing out of bounds in flat_points");
        assert(points + i * d + d <= points + n * d,  "2) Accessing out of bounds in flat_points");
        return points[i * d + j];
    }

    float* operator[](size_t i) {
        assert(points != nullptr,  "Points should not be null");
        assert(i < n,  "Index out of bounds: " + std::to_string(i) + " >= " + std::to_string(n));
        assert(d > 0,  "Dimension d should be greater than 0");
        assert(points + i * d < points + n * d,  "1) Accessing out of bounds in flat_points");
        assert(points + i * d + d <= points + n * d,  "2) Accessing out of bounds in flat_points");
        return points + i * d;
    }

    const float* operator[](size_t i) const {
        assert(points != nullptr,  "Points should not be null");
        assert(i < n,  "Index out of bounds: " + std::to_string(i) + " >= " + std::to_string(n));
        assert(d > 0,  "Dimension d should be greater than 0");
        assert(points + i * d < points + n * d,  "Accessing out of bounds in flat_points");
        assert(points + i * d + d <= points + n * d,  "Accessing out of bounds in flat_points");
        return points + i * d;
    }
};

typedef struct indices {
    size_t flipped = 0;
    size_t* indices_vec;
    size_t* indices_vec_tmp;
    size_t original_n; 
    size_t n;
    size_t start;

    static indices create(size_t n) {
        size_t* indices_vec = new size_t[n];
        size_t* indices_vec_tmp = new size_t[n];
        for (size_t i = 0; i < n; ++i) {
            indices_vec[i] = i;
            indices_vec_tmp[i] = std::numeric_limits<size_t>::max();
        }

        return indices(n, indices_vec, indices_vec_tmp, 0, 0, n);
    }

    std::pair<indices, indices> split(
        const size_t c1IDX,
        const size_t c2IDX,
        const struct flat_points points) {

        float* c1 = (float *) alloca(points.d * sizeof(float));
        float* c2 = (float *) alloca(points.d * sizeof(float));
        for (size_t j = 0; j < points.d; ++j) {
            c1[j] = points[c1IDX][j];
            c2[j] = points[c2IDX][j];
        }

        size_t i = start;
        size_t cur0 = start;
        size_t cur1 = start + n - 1;
        bool last_is_c0 = false;
        assert(cur0 <= cur1, "This should never happen");
        assert(n <= points.n, "Number of indices should not exceed number of points: " + std::to_string(n) + " > " + std::to_string(points.n));
        assert(indices_vec != nullptr, "Indices vector should not be null");
        assert(indices_vec_tmp != nullptr, "Indices vector temporary should not be null");
        assert(start + n - 1 < original_n, "Start + n - 1 should be less than original_n: " + std::to_string(start + n - 1) + " >= " + std::to_string(original_n));

        const size_t d = points.d;
        while (cur0 <= cur1) {
            const size_t idx = indices_vec[i];
            assert(idx != std::numeric_limits<size_t>::max(), "Index should not be max size_t: " + std::to_string(idx));

            const float* p = points[idx];

            float dist1 = 0.0;
            float dist2 = 0.0;

            for (size_t j = 0; j < d; ++j) {
                dist1 += (c1[j] - p[j]) * (c1[j] - p[j]);
                dist2 += (c2[j] - p[j]) * (c2[j] - p[j]);
            }

            if (dist1 < dist2) {
                indices_vec_tmp[cur0] = idx;
                last_is_c0 = true;
                cur0++;
            } else {
                indices_vec_tmp[cur1] = idx;
                last_is_c0 = false;
                cur1--;
            }

            i++;
        }

        std::swap(indices_vec, indices_vec_tmp);

        assert(cur0 > cur1, "cur0 should be greater than cur1 after the loop");

        cur0 = last_is_c0 ? cur0 - 1 : cur0 - 1;
        cur1 = last_is_c0 ? cur1 + 1 : cur1 + 1;

        _cout << "CoresetTree: split: cur0 = " << cur0 << ", cur1 = " << cur1 << "\n";
        assert(cur1 > cur0, "cur1 should be greater than cur0 after the loop");

        size_t n0 = cur0 - start + 1;
        size_t n1 = (start + n) - cur0 - 1;

        size_t start0 = start;
        size_t start1 = cur1;

        // _cout << "CoresetTree: split: left [" << start0 << ", " << start0 + n0 << "] right [" << start1 << ", " << start1 + n1 << "]\n";
        _cout << "CoresetTree: split: left [" << start0 << ", " << start0 + n0 - 1 << "] (" << n0 << ") right [" << start1 << ", " << start1 + n1 - 1 << "] (" << n1 << ") sum: " << n0 + n1 << "\n";
        assert(n0 + n1 == n, "Sum of split sizes should be equal to n");

        assert(start0 < start1, "Start of left indices should be less than start of right indices: " + std::to_string(start0) + " >= " + std::to_string(start1));
        assert(start0 + n0 <= start1, "End of left indices should be less than or equal to start of right indices: " + std::to_string(start0 + n0) + " > " + std::to_string(start1));
        assert(start1 + n1 <= start + n, "End of right indices should be less than or equal to start + n: " + std::to_string(start1 + n1) + " > " + std::to_string(start + n));
        assert(start0 + n0 <= start + n, "End of left indices should be less than or equal to start + n: " + std::to_string(start0 + n0) + " > " + std::to_string(start + n));

        assert(start0 < original_n, "Start of left indices should be less than original_n: " + std::to_string(start0) + " >= " + std::to_string(original_n));
        assert(start1 < original_n, "Start of right indices should be less than original_n: " + std::to_string(start1) + " >= " + std::to_string(original_n));
        assert(start0 + n0 <= original_n, "End of left indices should be less than or equal to original_n: " + std::to_string(start0 + n0) + " > " + std::to_string(original_n));
        assert(start1 + n1 <= original_n, "End of right indices should be less than or equal to original_n: " + std::to_string(start1 + n1) + " > " + std::to_string(original_n));

        return {
            indices(n0, indices_vec, indices_vec_tmp, start0, flipped + 1, original_n),
            indices(n1, indices_vec, indices_vec_tmp, start1, flipped + 1, original_n)
        };

    }

    size_t size() const {
        return n;
    }

    size_t& operator[](size_t i) {
        assert(i < n, "Index out of bounds: " + std::to_string(i + start) + " >= " + std::to_string(n));
        assert(indices_vec != nullptr, "Indices vector should not be null");
        assert(indices_vec_tmp != nullptr, "Indices vector temporary should not be null");
        return indices_vec[i + start];
    }

    const size_t& operator[](size_t i) const {
        assert(i < n, "Index out of bounds: " + std::to_string(i + start) + " >= " + std::to_string(n));
        assert(indices_vec != nullptr, "Indices vector should not be null");
        assert(indices_vec_tmp != nullptr, "Indices vector temporary should not be null");
        return indices_vec[i + start];
    }

private:
    indices(size_t n, size_t* indices_vec, size_t* indices_vec_tmp, size_t start = 0, size_t flipped = 0, size_t original_n = 0)
        : n(n), indices_vec(indices_vec), indices_vec_tmp(indices_vec_tmp), start(start), flipped(flipped), original_n(original_n) {
    }

} CTIndices;

typedef struct ct_node {
    struct flat_points points; // NON OWNING PROPS
    float* weights; // NON OWNING PROPS
    
    ct_node *parent, *lc, *rc;
    size_t centerIDX;
    CTIndices indices;

    float cost;

    bool is_leaf() const {
        assert((lc == nullptr) == (rc == nullptr ), "Both children should be either null or non-null");
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

    template<int Rounds=3>
    size_t pick_centroid() {
        assert(!std::isnan(cost) && !std::isinf(cost), "Cost should not be NaN or Inf");

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


                assert(!std::isnan(sum) && !std::isinf(sum), "Sum should not be NaN or Inf");

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
        assert(min_cost < cost, "Picked centroid cost should be less than the current cost");
        assert(pickedIDX < points.n, "Picked index out of bounds: " + std::to_string(pickedIDX) + " >= " + std::to_string(points.n));
        assert(pickedIDX != centerIDX, "Picked index should not be equal to center index: " + std::to_string(pickedIDX) + " == " + std::to_string(centerIDX));
        return pickedIDX;
    }

    void split(size_t sampleIDX) {
        assert(is_leaf(), "Only leaf nodes can be split");
        assert(sampleIDX < points.n, "Sample index out of bounds: " + std::to_string(sampleIDX) + " >= " + std::to_string(points.n));
        assert(sampleIDX != centerIDX, "Sample index should not be equal to center index: " + std::to_string(sampleIDX) + " == " + std::to_string(centerIDX));
        assert(centerIDX < points.n, "Center index out of bounds: " + std::to_string(centerIDX) + " >= " + std::to_string(points.n));

        for (size_t i = 0; i < indices.size(); ++i) {
            assert(indices[i] < points.n, "Index out of bounds: " + std::to_string(indices[i]) + " >= " + std::to_string(points.n));
        }

        auto [left_indices, right_indices] = indices.split(centerIDX, sampleIDX, points);
        _cout << "CoresetTree: split: left size = " << left_indices.size() << ", right size = " << right_indices.size() << "\n";

        assert(left_indices.size() + right_indices.size() == indices.size(), "Sum of left and right indices should be equal to the original indices size: " + std::to_string(left_indices.size() + right_indices.size()) + " != " + std::to_string(indices.size()));
        assert(left_indices.size() > 0 && right_indices.size() > 0, "Both left and right indices should have at least one element");    
        assert(left_indices.size() < points.n && right_indices.size() < points.n, "Left and right indices should not exceed number of points: " + std::to_string(points.n));
        for (size_t i = 0; i < left_indices.size(); ++i) {
            assert(left_indices[i] < points.n, "Left index out of bounds: " + std::to_string(left_indices[i]) + " >= " + std::to_string(points.n));
        }
        
        for (size_t i = 0; i < right_indices.size(); ++i) {
            assert(right_indices[i] < points.n, "Right index out of bounds: " + std::to_string(right_indices[i]) + " >= " + std::to_string(points.n));
        }

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
                assert(!std::isnan(diff) && !std::isinf(diff), "Diff should not be NaN or Inf");
                sum += diff * diff;
            }
        }

        assert(!std::isnan(sum) && !std::isinf(sum), "Sum should not be NaN or Inf");
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

    void extract_raw_inplace(
        float* flat_points,
        float* flat_weights,
        size_t n, size_t d
    ) {

        assert(n > 0 && d > 0, "n and d should be greater than 0");
        assert(flat_points != nullptr, "Flat points should not be null");
        assert(flat_weights != nullptr, "Flat weights should not be null");

        std::vector<ct_node*> nodes = {this};

        int idx = 0;
        while (!nodes.empty()) {
            ct_node* node = nodes.back();
            nodes.pop_back();

            if (node->is_leaf()) {
                const size_t centerIDX = node->centerIDX;
                const float* center = node->points[centerIDX];
                const float new_weight = node->indices.size();

                // flat_weights[idx] = new_weight;
                for (size_t j = 0; j < d; ++j) {
                    flat_points[idx * d + j] = center[j];
                }
             
                if (idx >= n) {
                    throw std::runtime_error("Coreset size exceeds the number of points");
                }

                idx++;
            } else {
                if (node->lc) nodes.push_back(node->lc);
                if (node->rc) nodes.push_back(node->rc);
            }
        }

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

