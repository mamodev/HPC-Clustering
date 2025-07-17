#pragma once

#include <vector>
#include <string>
#include <fstream>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <cmath>
#include <filesystem>   

template<typename E>
concept Dtype = std::is_same<E, float>::value || std::is_same<E, double>::value;

template<Dtype T>
struct samples_t {
    std::vector<T> data;
    size_t features;
    size_t samples;
};

template<Dtype T>
using Samples = samples_t<T>;




template<Dtype T>
Samples<T> readSamples(const std::string &path) {

    std::cout << "Reading samples from: " << path << std::endl;

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Could not open file: " + path);
    }

    // First uint32_t is the number of dimensions (including the batch dim)
    uint32_t dims;
    file.read(reinterpret_cast<char*>(&dims), sizeof(dims));
    if (!file) {
        throw std::runtime_error("Failed to read dims");
    }
    // std::cout << "Dims: " << dims << std::endl;

    // Next `dims` uint32_t's describe the shape (n, d1, d2, ..., dk)
    std::vector<uint32_t> shape(dims);
    file.read(reinterpret_cast<char*>(shape.data()), dims * sizeof(uint32_t));
    if (!file) {
        throw std::runtime_error("Failed to read shape");
    }

    // Number of samples
    uint32_t n = shape[0];
    // Product of remaining dimensions = features per sample
    size_t features = 1;
    for (uint32_t i = 1; i < dims; ++i) {
        // std::cout << "Shape[" << i << "]: " << shape[i] << std::endl;
        features *= shape[i];
    }
    // std::cout << "N: " << n << "  Features: " << features << std::endl;

    // Read n * features floats into a flat vector
    Samples<T> s;
    s.features = features;
    s.samples = n;
    s.data.resize(static_cast<size_t>(n) * features);
    
    file.read(reinterpret_cast<char*>(s.data.data()),
              s.data.size() * sizeof(float));

    if (!file) {
        throw std::runtime_error("Failed to read sample data");
    }

    return s;
}

void writeResults(const std::string &path, const std::vector<int> &labels) {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Could not open file: " + path);
    }

    uint64_t n = labels.size();
    file.write(reinterpret_cast<const char*>(&n), sizeof(n));
    if (!file) {
        throw std::runtime_error("Failed to write number of labels");
    }

    for (const auto &label : labels) {
        uint64_t l = static_cast<uint64_t>(label);
        file.write(reinterpret_cast<const char*>(&l), sizeof(l));
        if (!file) {
            throw std::runtime_error("Failed to write label");
        }
    }

    std::cout << "Results written to: " << path << std::endl;
}

template<Dtype T>
int containsNaNOrInf(const std::vector<T>& data) {
    for (size_t i = 0; i < data.size(); ++i) {
        if (std::isnan(data[i]) || std::isinf(data[i])) {
            return -1;
        }
    }
    return 0;
}

template<Dtype T>
std::tuple<Samples<T>, std::string, size_t> parseArgs(int argc, char **argv) {
    std::string usage = "Usage: <cmd> <input file> <output file/folder> [<optional: coreset_size = default 10000>]\n";
    if (argc < 3) {
        std::cerr << usage;
        throw std::invalid_argument("Not enough arguments provided");
    }

    std::string inputFile = argv[1];
    std::string outputFile = argv[2];

    int coresetSize = 10000; // default value
    if (argc > 3) {
        try {
            coresetSize = std::stoi(argv[3]);
        } catch (const std::invalid_argument&) {
            std::cerr << "Invalid coreset size, using default value: 10000" << std::endl;
        }
    }
        
    if (inputFile.empty() || outputFile.empty()) {
        throw std::invalid_argument("Input and output file/folder cannot be empty");
    }

    // cast to filesystem::path
    std::filesystem::path inputPath(inputFile);
    std::filesystem::path outputPath(outputFile);

    // assert output path is a directory and if not exists create it
    if (!std::filesystem::exists(outputPath)) {
        std::filesystem::create_directories(outputPath);
    } else if (!std::filesystem::is_directory(outputPath)) {
        throw std::invalid_argument("Output path must be a directory");
    }

    return {readSamples<T>(inputFile), outputFile, coresetSize};
}

class MemoryStream {
private:
    samples_t<float> samples;
    size_t index;


    public:
        size_t processed_batches;
        size_t features;
        size_t coreset_size;

        MemoryStream() = delete;
        MemoryStream(const MemoryStream&) = delete;
        MemoryStream& operator=(const MemoryStream&) = delete;

        MemoryStream(int argc, char** argv) {
            auto [samples, outDir, coreset_size] = parseArgs<float>(argc, argv);
            this->coreset_size = coreset_size;
            this->samples = std::move(samples);
            this->features = samples.features;
            this->index = 0;
            this->processed_batches = 0;
        }

        MemoryStream(samples_t<float> samples, size_t coreset_size)
            : coreset_size(coreset_size), samples(std::move(samples)), index(0), processed_batches(0) {
            if (samples.data.empty() || samples.features == 0) {
                throw std::invalid_argument("Samples data is empty or features are not set");
            }

            if (coreset_size == 0) {
                throw std::invalid_argument("Coreset size must be greater than 0");
            }

            this->features = samples.features;
        }


        std::vector<float> next_batch() {
            processed_batches++;

            if (index >= samples.samples) {
                return std::vector<float>(); 
            }
        
            const size_t target_batch_size = coreset_size * 2;
            size_t batch_size = std::min(target_batch_size, samples.samples - index);
            
            float *data_ptr = samples.data.data() + (index * samples.features);
    
            std::vector<float> batch(data_ptr, data_ptr + batch_size * samples.features);
            
            index += batch_size;
    
            return std::move(batch);
        }
};