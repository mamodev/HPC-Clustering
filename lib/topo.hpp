#pragma once

#if !defined(__linux__)
#error "This code is intended to be built only on Linux systems."
#endif

#include <stdexcept>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include "assert.hpp"

#include <sched.h>
void set_thread_affinity(unsigned int cpu_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);      // Initialize the CPU set to be empty
    CPU_SET(cpu_id, &cpuset); // Add the desired CPU to the set

    // Set affinity for the current thread (0 for calling thread)
    if (sched_setaffinity(0, sizeof(cpu_set_t), &cpuset) == -1) {
        // It's good practice to print an error but not necessarily throw
        // if this is a non-critical part, but for pinning, it's critical.
        throw std::runtime_error("Failed to set CPU affinity for thread to CPU " + std::to_string(cpu_id));
    }
}

using CpuSet = std::vector<unsigned int>;

struct CpuNode {
    unsigned int id; // CPU ID
    std::vector<CpuSet> cores;

    std::string toString() const {
        std::ostringstream oss;
        oss << "Node ID: " << id << ", Cores: ";
        for (const auto& core : cores) {
            oss << "[";
            for (const auto& cpu : core) {
                oss << cpu << " ";
            }
            oss << "] ";
        }
        return oss.str();
    }
};

using CpuTopo = std::vector<CpuNode>;

unsigned int getNodeCpu(CpuNode& n, size_t i) {
    int group = 0;
    while (group < n.cores.size() && i >= n.cores[group].size()) {
        i -= n.cores[group].size();
        group++;
    }
    fassert(group < n.cores.size(), "Invalid core index: " + std::to_string(i) + " for node with " + std::to_string(n.cores.size())
        + " cores and group " + std::to_string(group));
    return n.cores[group][i];
}   


size_t getNodeCount(const CpuTopo& topo) {
    return topo.size();
}

size_t getNodeCoreCount(const CpuNode& node) {
    size_t count = 0;
    for (const auto& core : node.cores) {
        count += core.size();
    }
    return count;
}

CpuSet getAllNodeCores(const CpuNode& node) {
    CpuSet all_cores;
    for (const auto& core : node.cores) {
        all_cores.insert(all_cores.end(), core.begin(), core.end());
    }
    return all_cores;
}

size_t getNodeGroupsCount(size_t node_id, const CpuTopo& topo) {
    if (node_id >= topo.size()) {
        throw std::out_of_range("Node ID out of range");
    }
    return topo[node_id].cores.size();
}

size_t getNodeGroupSize(size_t node_id, size_t group_id, const CpuTopo& topo) {
    if (node_id >= topo.size()) {
        throw std::out_of_range("Node ID out of range");
    }
    if (group_id >= topo[node_id].cores.size()) {
        throw std::out_of_range("Group ID out of range");
    }
    return topo[node_id].cores[group_id].size();
}

std::vector<size_t> flat_thread_pin_map(const CpuTopo& topo) {
    std::vector<size_t> pin_map;
    for (const auto& node : topo) {
        for (const auto& core : node.cores) {
            for (const auto& cpu : core) {
                pin_map.push_back(cpu);
            }
        }
    }
    return pin_map;
}

size_t master_of_node_from_map(const CpuTopo& topo, const std::vector<size_t>& thread_pin_map, size_t node_id) {
    if (node_id >= topo.size()) {
        throw std::out_of_range("Node ID out of range");
    }

    size_t prev_count = 0;
    for (size_t i = 0; i < node_id; ++i) {
        for (const auto& core : topo[i].cores) {
            prev_count += core.size();
        }
    }

    return thread_pin_map[prev_count]; 
}

size_t node_of_thread_from_map(const CpuTopo& topo, const std::vector<size_t>& thread_pin_map, size_t thread_id) {
    if (thread_id >= thread_pin_map.size()) {
        throw std::out_of_range("Thread ID out of range");
    }

    size_t cpu_id = thread_pin_map[thread_id];
    for (size_t i = 0; i < topo.size(); ++i) {
        for (const auto& core : topo[i].cores) {
            if (std::find(core.begin(), core.end(), cpu_id) != core.end()) {
                return i; // Found the node containing this CPU
            }
        }
    }

    throw std::runtime_error("CPU not found in any node");
}




size_t getCpuCount(CpuTopo& topo) {
    size_t count = 0;
    for (const auto& node : topo) {
        for (const auto& core : node.cores) {
            count += core.size();
        }
    }
    return count;
}

std::string cpuTopoToString(const CpuTopo& topo) {
    std::ostringstream oss;
    for (const auto& node : topo) {
        oss << node.toString() << "\n";
    }
    return oss.str();
}

std::vector<std::string> splitString(const std::string& s, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    size_t start = 0;
    size_t end = s.find(delimiter);

    while (end != std::string::npos) {
        token = s.substr(start, end - start);
        tokens.push_back(token);
        start = end + 1;
        end = s.find(delimiter, start);
    }
    tokens.push_back(s.substr(start)); // Add the last token

    return tokens;
}

CpuTopo detect_cpu_topology(bool disable_hyperthreading = false, bool l3cache_grouping = false) {
    CpuTopo topology;


    int curr_node = 0;

    while (true) {
        CpuNode node;
        node.id = curr_node;

        std::string path = "/sys/devices/system/node/node" + std::to_string(curr_node) + "/cpulist";
        
        std::ifstream file(path);
        if (!file.is_open()) {
            if (curr_node == 0) {
                throw std::runtime_error("Failed to open CPU topology file: " + path);
            } else {
                return topology;
            } 
        }   

        // read all file (should be a single line)
        std::string line;
        std::getline(file, line);
        file.close();

        // format: "0-3,6,8-10"
        auto parts = splitString(line, ',');

        CpuSet cpu_set;
        for (const auto& part : parts) {
            auto range_parts = splitString(part, '-');
            fassert(range_parts.size() <= 2 && range_parts.size() >= 1, "Invalid CPU range format: " + part);


            if (range_parts.size() == 1) {
                unsigned int cpu = std::stoul(range_parts[0]);
                cpu_set.push_back(cpu);
                continue;
            }

            unsigned int start = std::stoul(range_parts[0]);
            unsigned int end = (range_parts.size() == 2) ? std::stoul(range_parts[1]) : start;

            for (unsigned int cpu = start; cpu <= end; ++cpu) {

                if (disable_hyperthreading) {
                    std::string cpu_path = "/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/topology/thread_siblings_list";
                    std::ifstream cpu_file(cpu_path);
                    if (!cpu_file.is_open()) {
                        throw std::runtime_error("Failed to open CPU topology file: " + cpu_path);  
                    }

                    std::string cpu_line;
                    std::getline(cpu_file, cpu_line);
                    cpu_file.close();

                    auto siblings = splitString(cpu_line, ',');
                    if (siblings.size() > 1) {
                        // it should be the first CPU in the thread siblings list
                        if (std::stoul(siblings[0]) != cpu) {
                            continue; // skip this CPU, it's a hyperthreaded sibling
                        }
                    }

                }


                cpu_set.push_back(cpu);
            }
        }


        std::vector<CpuSet> cores;
        if (l3cache_grouping) {
            for (unsigned int cpu : cpu_set) {
                std::string l3_path = "/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/cache/index3/id";
                std::ifstream l3_file(l3_path);
                if (!l3_file.is_open()) {
                    throw std::runtime_error("Failed to open L3 cache file: " + l3_path);
                }

                std::string l3_line;
                std::getline(l3_file, l3_line);
                l3_file.close();

                unsigned int l3_id = std::stoul(l3_line);

                while (cores.size() <= l3_id) {
                    cores.emplace_back();
                }


                cores[l3_id].push_back(cpu);
            }
        } else {
            cores.push_back(cpu_set);
        }

        // remove empty cores vector 
        cores.erase(std::remove_if(cores.begin(), cores.end(),
            [](const CpuSet& core) { return core.empty(); }), cores.end());

        fassert(!cores.empty(), "No cores found for node " + std::to_string(curr_node));

        node.cores = std::move(cores);
        topology.push_back(std::move(node));
        curr_node++;
    }

    return topology;
}