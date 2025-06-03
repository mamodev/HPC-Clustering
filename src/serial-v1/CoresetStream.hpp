#pragma once


#include "CoresetTree.hpp"
#include <optional>

typedef struct coreset_stream {
    std::vector<std::optional<Coreset>> coresets;

    size_t m = 0;
    size_t d = 0;
    float* sink = nullptr;
    size_t sink_size = 0;

    coreset_stream(size_t m, size_t d) : m(m), d(d) {
        sink = (float*)malloc(2 * m * d * sizeof(float));
        sink_size = 0;        
    }

    void insert(float* x) {
        assert(sink_size <= 2 * m);
        
        for (size_t i = 0; i < d; ++i) {
            sink[sink_size * d + i] = x[i];
        }

        sink_size++;

        if (sink_size == 2 * m) {

            // std::cout << "CoresetStream: sink_size = " << sink_size << "\n";

            // we must build a coreset tree to free sink
            auto root = CoresetTree(sink, nullptr, sink_size, d, m);
            auto coreset = root.extract_coreset();
            sink_size = 0;

            // now me must reduce the coreset vector
            int idx = -1;
            float *weights = nullptr;

            // std::cout << "CoresetStream: coreset size = " << coreset.points.n << "\n";

            while (idx < (int)coresets.size() - 1) {        
                if (!coresets[idx + 1].has_value()) {
                    // std::cout << "CoresetStream: coreset " << idx + 1 << " is empty\n";
                    break;
                }

                idx++;

                // std::cout << coreset.points.n << " " << coresets[idx]->points.n << " size: " << coresets.size() << " idx: " << idx << "\n";
                assert(m == coreset.points.n && m == coresets[idx]->points.n && "CoresetStream: coresets must have the same size");

                if (!weights) {
                    weights = new float[m * 2];
                }

                for (size_t i = 0; i < m; ++i) {
                    weights[i] = coreset.weights[i];
                    for (size_t j = 0; j < d; ++j) {
                        sink[i * d + j] = coreset.points[i][j];
                    }
                }

                for (size_t i = m; i < 2 * m; ++i) {
                    weights[i] = coresets[idx]->weights[i - m];

                    for (size_t j = 0; j < d; ++j) {
                        sink[i * d + j] = coresets[idx]->points[i - m][j];
                    }
                }


                auto root = CoresetTree(sink, weights, 2 * m, d, m);
                coreset = std::move(root.extract_coreset());
                coresets[idx] = std::nullopt;
            }

            if(weights != nullptr) {
                delete[] weights;
            }

            // if (idx == -1) {
            //     std::cout << "CoresetStream: first coreset\n";
            //     assert(coresets.size() == 0);
            //     assert(coreset.points.n == m && "CoresetStream: first coreset must have size m");

            //     coresets.push_back(std::move(coreset));
            //     assert(coresets.size() == 1);
            //     assert(coresets[0].has_value());
            //     assert(coresets[0]->points.n == m && "CoresetStream: first coreset must have size m"); 
            // } else {
                // assert(!coresets[idx].has_value());
                // coresets[idx] = std::move(coreset);
                size_t save_into = idx + 1;
                if (save_into >= coresets.size()) {
                    // std::cout << "CoresetStream: coreset " << save_into << " new coreset\n";
                    coresets.push_back(std::move(coreset));
                } else {
                    // std::cout << "CoresetStream: coreset " << save_into << " save in already existing coreset\n";   
                    coresets[save_into] = std::move(coreset);
                }
            // }

            // std::cout << "CoresetStream: sink_size = " << sink_size << "\n\n";
        }
    
    }

    Coreset final_coreset() {

        // std::cout << "CoresetStream: final coreset\n";

        if (sink_size > m) { //tmp
            // std::cout << "CoresetStream: sink_size = " << sink_size << "\n";
            auto root = CoresetTree(sink, nullptr, sink_size, d, m);
            auto coreset = root.extract_coreset();
            sink_size = 0;
            coresets.insert(coresets.begin(), std::move(coreset));
        }

        // std::cout << "CoresetStream: coresets size = " << coresets.size() << "\n";

        Coreset coreset;
        size_t first_non_empty = 0;
        // find first non empty coreset
        for (size_t i = 0; i < coresets.size(); ++i) {
            if (coresets[i].has_value()) {
                coreset = std::move(coresets[i].value());
                first_non_empty = i;
                break;
            }
        }

        // std::cout << "CoresetStream: coreset size = " << coreset.points.n << "\n";

        assert(coreset.weights != nullptr && "No non empty coreset found");

        // loop through all coresets and skip empty ones
        float *weights = nullptr;

        for (size_t i = first_non_empty+1; i < coresets.size(); ++i) {
            if (!coresets[i].has_value()) {
                continue;
            }

            // std::cout << "CoresetStream: coreset " << i << " size = " << coresets[i]->points.n << "\n";

            if (weights == nullptr) {
                weights = new float[m * 2];
            }

            for (size_t j = 0; j < m; ++j) {
                weights[j] = coreset.weights[j];
                for (size_t k = 0; k < d; ++k) {
                    sink[j * d + k] = coreset.points[j][k];
                }
            }

            for (size_t j = m; j < 2 * m; ++j) {
                weights[j] = coresets[i]->weights[j - m];

                for (size_t k = 0; k < d; ++k) {
                    sink[j * d + k] = coresets[i]->points[j - m][k];
                }
            }

            auto root = CoresetTree(sink, weights, 2 * m, d, m);
            coreset = std::move(root.extract_coreset());
        }

        if (weights != nullptr) {
            delete[] weights;
        }


        return coreset;
    }

    ~coreset_stream() {
        free(sink);
    }

} CoresetStream;
 