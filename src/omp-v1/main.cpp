
#include "concurrentqueue.h"
#include "assert.hpp"
#include "coreset.hpp"
#include "coreset_stream.hpp"
#include "parser.hpp"

#include <iostream>
#include <cmath>
#include <vector>
#include <thread>
#include <barrier>
#include <mutex>
#include <numa.h>
#include <omp.h>
#include <chrono>

int main(int argc, char* argv[]) {
    if (numa_available() < 0) {
        std::cerr << "NUMA is not available on this system." << std::endl;
        return 1;
    }

    omp_set_dynamic(0); // Disable dynamic adjustment of the number of threads

    omp_set_num_threads(256); // Set number of threads to number of available processors
    numa_set_strict(1); // Enable strict NUMA policy

    
    int n_numa_nodes = numa_num_configured_nodes();
    // int n_numa_nodes = 1;

    // auto th_safe_stream = MemoryStream<true>(argc, argv);

    auto start = std::chrono::high_resolution_clock::now();

    auto threads = std::vector<std::thread>();
    auto barrier = std::barrier<>(n_numa_nodes);
    for (int node = 0; node < n_numa_nodes; ++node) {
        threads.emplace_back([argc, argv, n_numa_nodes, node, &barrier, &start]() {
            // 1) Pin *this* master to node
            numa_run_on_node(node);
            // 2) Prefer allocations on that node
            numa_set_preferred(node);

            // 3) Figure out how many cores live on this node
            struct bitmask* cpus = numa_allocate_cpumask();
            numa_node_to_cpus(node, cpus);
            int threads_on_node = 0;
            for (int i = 0; i < cpus->size; ++i)
                if (numa_bitmask_isbitset(cpus, i))
                ++threads_on_node;
            numa_free_cpumask(cpus);

            std::this_thread::yield();

            auto stream = MemoryStream<false>(argc, argv);
            stream.partition(n_numa_nodes, node);
            barrier.arrive_and_wait();
            if (node == 0) {
                start = std::chrono::high_resolution_clock::now();
            }

            // size_t cpu_count = numa_num_configured_cpus();
            // omp_set_num_threads(cpu_count);
            threads_on_node  = threads_on_node;

            std::cout << "Node " << node << " has " << threads_on_node << " threads." << std::endl;
            
            coresetStreamOmp<size_t, 3, 3U>(stream, threads_on_node / 2);
            
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << "Total duration: " << duration.count() << " ms" << std::endl;

    return 0;
}