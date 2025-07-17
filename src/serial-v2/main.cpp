#include <iostream>
#include <vector>
#include <chrono>
#include "coreset_stream.hpp"

int main(int argc, char* argv[]) {
    MemoryStream stream(argc, argv);

    auto start = std::chrono::high_resolution_clock::now();
    auto buckets = coresetStreamOmp<int32_t, 3, 3U>(stream);
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    std::cout << "Coreset computation took: " << duration << " ms" << std::endl;

    return 0;
}