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

    std::cout << "Read " << n << " samples with " << features
              << " features each." << std::endl;

    return s;
}

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
template<Dtype T>
Samples<T> readSamplesMmap(const std::string &path) {
    std::cout << "Reading samples from: " << path << std::endl;

    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) throw std::runtime_error("open failed");
  
    struct stat st;
    ::fstat(fd, &st);
    size_t size = st.st_size;
  
    void* p = ::mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (p == MAP_FAILED) throw std::runtime_error("mmap failed");
  
    char* cur = static_cast<char*>(p);
  
    // read dims
    uint32_t dims = *reinterpret_cast<uint32_t*>(cur);
    cur += sizeof(uint32_t);
  
    // read shape
    std::vector<uint32_t> shape(dims);
    std::memcpy(shape.data(), cur, dims * sizeof(uint32_t));
    cur += dims * sizeof(uint32_t);
  
    uint32_t n = shape[0];
    size_t features = 1;
    for (uint32_t i = 1; i < dims; ++i)
      features *= shape[i];
  
    Samples<T> s;
    s.samples  = n;
    s.features = features;
    // Instead of copying, you could wrap cur in a view; if you must own a copy:
    s.data.resize(n * features);
    std::memcpy(s.data.data(), cur, n * features * sizeof(T));
  
    ::munmap(p, size);
    ::close(fd);

    std::cout << "Read " << n << " samples with " << features << " features each." << std::endl;
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


class CoresetStream {
public:
    size_t processed_batches;
    size_t features;
    size_t coreset_size;

    CoresetStream(const CoresetStream&) = delete;
    CoresetStream& operator=(const CoresetStream&) = delete;
    CoresetStream(): features(0), coreset_size(0), processed_batches(0) {}

    virtual ~CoresetStream() = default;
    virtual std::vector<float> next_batch() = 0;
};


template<bool ThreadSafe = false>
class MemoryStream : public CoresetStream {
private:
    samples_t<float> samples;

    using IndexType = std::conditional_t<ThreadSafe, std::atomic<size_t>, size_t>;
    IndexType index;

public:
    MemoryStream(int argc, char** argv) : CoresetStream(), index(0) {
        auto [samples, outDir, coreset_size] = parseArgs<float>(argc, argv);
        this->samples = std::move(samples);
        this->index = 0;

        // Superclass initialization
        this->coreset_size = coreset_size;
        this->features = samples.features;
        this->processed_batches = 0;
    }
   
    std::vector<float> next_batch() {
        if constexpr (ThreadSafe) {
            processed_batches++;
        }

        const size_t target_batch_size = coreset_size * 2;

        size_t start;
        if constexpr (ThreadSafe) {
            start = index.fetch_add(target_batch_size, std::memory_order_relaxed);
        } else {
            start = index;
        }   

        if (start >= samples.samples) {
            return {};
        }
        

        size_t batch_size = std::min(target_batch_size, samples.samples - start);
        if constexpr (!ThreadSafe) {
            index += batch_size;
        }
        
        float *data_ptr = samples.data.data() + (index * samples.features);
        return std::vector<float>(data_ptr, data_ptr + batch_size * samples.features);
    }
};
