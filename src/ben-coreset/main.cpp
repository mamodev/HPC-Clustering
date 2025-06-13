
#include "assert.hpp"
#include "CoresetTree.hpp"

#include <random>
#include <chrono>
#include <iostream>
#include <fstream>

const size_t CACHE_TRASH_SIZE_BYTES = 128 * 1024 * 1024; // 128 MB

// create a noinline force marcro

__attribute__((noinline))
void trash_cache(size_t size_in_bytes) {
    std::vector<char> buffer(size_in_bytes);
    for (size_t i = 0; i < buffer.size(); ++i) {
        reinterpret_cast<volatile char*>(buffer.data())[i] = static_cast<char>(i % 256);    
    }
}


int main(int argc, char** argv) {

    std::string usage = "Usage: <cmd> <coreset size> <features> <rounds def: 100>\n"
                        "Example: ./ben-coreset 100 10\n"
                        "This will create a coreset with 100 points in a 10-dimensional space.";
    
    if (argc < 3) {
        std::cerr << usage << std::endl;
        return 1;
    }

    int coreset_size = std::stoi(argv[1]);
    int features = std::stoi(argv[2]);
    int rounds = argc > 3 ? std::stoi(argv[3]) : 10;

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(0.0, 1.0);

    std::uniform_int_distribution<> dis_int(1, 2);

    float* data = new float[2 * coreset_size * features];
    float* weights = new float[2 * coreset_size];

    for (int i = 0; i < 2 * coreset_size * features; ++i) {
        data[i] = dis(gen);
    };

    for (int i = 0; i < 2 * coreset_size; ++i) {
        weights[i] = dis_int(gen);
    }

    auto start = std::chrono::high_resolution_clock::now();
    auto end = std::chrono::high_resolution_clock::now();

    std::vector<int64_t> d1s(rounds);
    std::vector<int64_t> d2s(rounds);


    std::cout << "CoresetTree benchmark with " << coreset_size << " points in " << features << " dimensions, "
              << rounds << " rounds.\n";

    for (int i = 0; i < rounds; ++i) {
        trash_cache(CACHE_TRASH_SIZE_BYTES);
        start = std::chrono::high_resolution_clock::now();
        auto root = CoresetTree(data, nullptr,  2 * coreset_size, features, coreset_size);
        end = std::chrono::high_resolution_clock::now();
        d1s[i] = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    }


    for (int i = 0; i < rounds; ++i) {
        trash_cache(CACHE_TRASH_SIZE_BYTES);
        start = std::chrono::high_resolution_clock::now();
        auto root = CoresetTree(data, weights, 2 * coreset_size, features, coreset_size);
        end = std::chrono::high_resolution_clock::now();
        d2s[i] = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    }


    double d1 = 0.0;
    double d2 = 0.0;
    for (int i = 0; i < rounds; ++i) {
        d1 += d1s[i];
        d2 += d2s[i];
    }

    d1 /= rounds;
    d2 /= rounds;


    double var1_sq = 0.0;
    double var2_sq = 0.0;

    for (int i = 0; i < rounds; ++i) {
        var1_sq += std::pow(d1s[i] - d1, 2);
        var2_sq += std::pow(d2s[i] - d2, 2);
    }

    var1_sq /= rounds - 1; // Sample variance
    var2_sq /= rounds - 1; // Sample variance

    double dev = std::sqrt(var1_sq);
    double dev2 = std::sqrt(var2_sq);

    std::cout << "CoresetTree with weights: " << d2 << " us, variance: " << var2_sq << "sd, stddev: " << dev2 << " us\n";
    std::cout << "CoresetTree without weights: " << d1 << " us, variance: " << var1_sq << "sd, stddev: " << dev << " us\n";
    std::cout << "CoresetTree with weights - without weights: " << std::abs(d2 - d1) << " us\n";
    std::cout << "CoresetTree with weights + without weights: " << d2 + d1 << " us\n";

    // append to file coreset_benchmark.csv (create if not exists) 
    std::ofstream ofs("coreset_benchmark.csv", std::ios::app);
    if (!ofs.is_open()) {
        std::cerr << "Error opening file coreset_benchmark.csv" << std::endl;
        return 1;
    }

    // if file is empty, write header
    if (ofs.tellp() == 0) {
        ofs << "coreset_size,features,rounds,d1,var1_sq,dev,d2,var2_sq,dev2,diff,sum\n";
    }


    ofs << coreset_size << "," << features << "," << rounds << ","
        << d1 << "," << var1_sq << "," << dev << ","
        << d2 << "," << var2_sq << "," << dev2 << ","
        << std::abs(d2 - d1) << "," << (d2 + d1) << "\n";


}