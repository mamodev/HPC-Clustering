
#include "concurrentqueue.h"
#include "topo.hpp"
#include "assert.hpp"
#include "coreset.hpp"
#include "parser.hpp"

#include <iostream>
#include <cmath>
#include <vector>
#include <thread>
#include <barrier>
#include <mutex>


// class CoresetStream {
// public:
//     size_t processed_batches;
//     size_t features;
//     size_t coreset_size;

//     CoresetStream(const CoresetStream&) = delete;
//     CoresetStream& operator=(const CoresetStream&) = delete;
//     CoresetStream(): features(0), coreset_size(0), processed_batches(0) {}

//     virtual ~CoresetStream() = default;
//     virtual std::vector<float> next_batch() = 0;
// };

using Message = std::vector<float>; // Define a message type for the queues


using NUMAGroup = std::vector<unsigned int>; // Represents a group of CPUs in a NUMA node
using PipelineStage = std::vector<NUMAGroup>; // this is more of a map wher index is the numa node id
using Pipeline = std::vector<PipelineStage>;

void printPipeline(const Pipeline& pipeline) {
    for (size_t i = 0; i < pipeline.size(); ++i) {
        std::cout << "Stage " << i << ": ";
        for (const auto& group : pipeline[i]) {
            std::cout << "[";
            for (const auto& cpu : group) {
                std::cout << cpu << " ";
            }
            std::cout << "] ";
        }
        std::cout << std::endl;
    }
}

int main(int argc, char* argv[]) {
    auto cpu_topo = detect_cpu_topology(true, true);
    std::cout << "Detected CPU Topology:\n" << cpuTopoToString(cpu_topo) << std::endl;

    size_t cpu_per_node = getNodeCoreCount(cpu_topo[0]);
    size_t numa_nodes = getNodeCount(cpu_topo);

    std::cout << "CPU per NUMA node: " << cpu_per_node << std::endl;

    for (auto& node : cpu_topo) {
        fassert(getNodeCoreCount(node) == cpu_per_node, "Inconsistent core count across nodes");
    }

    int pipeline_length = static_cast<int>(std::log2(static_cast<double>(cpu_per_node * numa_nodes)));
    std::cout << "Pipeline length: " << pipeline_length << std::endl;

    Pipeline pipeline(pipeline_length);
    auto allocated_per_node = std::vector<size_t>(numa_nodes, 0);
    for (int stage = 0; stage < pipeline_length; ++stage) {
        for (size_t numa_node = 0; numa_node < numa_nodes; ++numa_node) {
            int nc = std::pow(2, pipeline_length - stage - 1) / numa_nodes;
            if (nc == 0) {
                int reminder = static_cast<int>(std::pow(2, pipeline_length - stage - 1)) % numa_nodes;
                if (numa_node < reminder) {
                    nc++;
                }
            }

            NUMAGroup group;
            for (size_t i = 0; i < nc; ++i) {
                fassert(allocated_per_node[numa_node] < cpu_per_node, "Allocated cores exceed available cores in NUMA node " + std::to_string(numa_node) 
                    + " at stage " + std::to_string(stage) + " with " + std::to_string(allocated_per_node[numa_node]) + " allocated cores");
                
                unsigned int cpu_id = getNodeCpu(cpu_topo[numa_node], allocated_per_node[numa_node]);
                allocated_per_node[numa_node]++;
                group.push_back(cpu_id);
            }

            pipeline[stage].push_back(std::move(group));
        }
    }

    std::cout << "Pipeline structure:\n";
    printPipeline(pipeline);

    std::vector<std::vector<moodycamel::ConcurrentQueue<Message>*>> queues(pipeline_length);
    for (int stage = 0; stage < pipeline_length; ++stage) {
        queues[stage].resize(numa_nodes);
        for (size_t numa_node = 0; numa_node < numa_nodes; ++numa_node) {
            queues[stage][numa_node] = nullptr; // Initialize queues
        }
    }


    auto stream = MemoryStream<true>(argc, argv);

    std::mutex io_mutex;

    std::barrier<> barrier(cpu_per_node * numa_nodes - 1);
    std::vector<std::thread> workers;
    for (int stage = 0; stage < pipeline_length; ++stage) {
        for (size_t numa_node = 0; numa_node < numa_nodes; ++numa_node) {
            for (size_t widx = 0; widx < pipeline[stage][numa_node].size(); ++widx) {
                unsigned int cpu = pipeline[stage][numa_node][widx];
                
                workers.emplace_back([stage, numa_node, widx, cpu, pipeline_length, &stream, &barrier, &queues, &io_mutex]() {
                    set_thread_affinity(cpu); // Pin thread to the specific CPU
                    if (widx == 0) {
                        queues[stage][numa_node] = new moodycamel::ConcurrentQueue<Message>();
                    }

                    barrier.arrive_and_wait();

                    size_t F = stream.features;
                    size_t C = stream.coreset_size;

                    if (stage == 0) { // Source 
                        auto& out_ch = queues[stage + 1][numa_node]; // TODO CHECK OUT OF BOUNDS
                        auto batch = stream.next_batch();
                        std::vector<float> tmp;
                        while (!batch.empty()) {
                            auto reduction = Coreset<size_t, false, 3, 3U>(batch.data(), batch.size() / F, F, C);
                            if (tmp.empty()) {
                                tmp = std::move(reduction);
                            } else {
                                tmp.insert(tmp.end(), reduction.begin(), reduction.end());
                                out_ch->enqueue(std::move(tmp));
                                tmp.clear();
                            }
                        }

                        if (widx % 2 == 1) {
                            std::lock_guard<std::mutex> lock(io_mutex);
                            std::cout << "Worker " << widx << " finished processing stage " << stage << " on NUMA node " << numa_node << std::endl;
                            // Signal the next
                            out_ch->enqueue(Message{0});                        
                        }

                    } else if (stage != pipeline_length - 1) { // Middle
                        auto& in_ch = queues[stage][numa_node];
                        auto& out_ch = queues[stage + 1][numa_node];

                        std::vector<float> tmp;
                        
                        for (;;) {
                            Message msg;
                            while (!in_ch->try_dequeue(msg)) {
                                std::this_thread::yield();
                            }

                            if (msg.empty()) {
                                std::lock_guard<std::mutex> lock(io_mutex);
                                std::cout << "Worker " << widx << " received empty message, exiting stage " << stage << " on NUMA node " << numa_node << std::endl;
                                break;
                            }

                            auto reduction = Coreset<size_t, true, 3, 3U>(msg.data(), msg.size() / (F + 1), F, C);

                            if (tmp.empty()) {
                                tmp = std::move(reduction);
                            } else {
                                tmp.insert(tmp.end(), reduction.begin(), reduction.end());
                                // out_ch->enqueue(std::move(tmp));
                                tmp.clear();
                            }

                            msg.clear();
                        }

                        {
                            std::lock_guard<std::mutex> lock(io_mutex);
                            std::cout << "Worker " << widx << " finished processing stage " << stage << " on NUMA node " << numa_node << std::endl;
                        }
                        
                        if (widx % 2 == 1) {
                            out_ch->enqueue(Message{0}); // Exit condition
                        }

                    } else { // Sink
                        auto& in_ch = queues[stage][numa_node];
                        // read all messages from the input channel
                        for (;;) {
                            Message msg;
                            while (!in_ch->try_dequeue(msg)) {
                                std::this_thread::yield();
                            }

                            if (msg.empty()) {
                                break; // Exit condition
                            }
                        }
                    }
                });
            }
        }
    }

    for (auto& worker : workers) {
        worker.join();
    }

    return 0;
}