#include <omp.h>
#include <iostream>
#include <deque>
#include <vector>
#include <queue>    
#include <mutex>
#include <condition_variable>
#include <cassert>
#include <thread>
#include "parser.hpp"
#include "coreset.hpp"
#include "perf.hpp"
#include "topo.hpp"

// create abstract class CoresetChannel wich could be implemented by ThreadSafeQueue or a wrapper arond the MemoryStream
class CoresetChannel {
public:
    virtual void push(const std::vector<float>& value) = 0;
    virtual std::vector<float> pop() = 0;
    virtual ~CoresetChannel() = default;
};

class ThreadSafeQueue : public CoresetChannel {
private:
    std::queue<std::vector<float>> q;
    std::mutex m;
    std::condition_variable cv_not_empty;

public:
    void push(const std::vector<float>& value) {
        std::unique_lock<std::mutex> lock(m);
        q.push(value);
        cv_not_empty.notify_one(); // Or notify_all() if multiple consumers
    }

    std::vector<float> pop() {
        std::unique_lock<std::mutex> lock(m);
        cv_not_empty.wait(lock, [this]{ return !q.empty(); });
        std::vector<float> value = q.front();
        q.pop();
        return value;
    }
};

class MemoryStreamChannel : public CoresetChannel {
private:
    MemoryStream<true> stream;
public:
    MemoryStreamChannel(int argc, char** argv) : stream(argc, argv) {}
   
    void push(const std::vector<float>& value) override {
        // MemoryStream does not support push, it only reads batches
        throw std::runtime_error("MemoryStreamChannel does not support push operation");
    }
   
    std::vector<float> pop() override {
        return stream.next_batch();
    }

    size_t get_features() const {
        return stream.features;
    }

    size_t get_coreset_size() const {
        return stream.coreset_size;
    }
};




int main(int argc, char* argv[]) {
    auto perf = PerfManager();
    perf.pause();

    CpuTopo cpu_topo = detect_cpu_topology(true, true);
    std::cout << "Detected CPU Topology:\n" << cpuTopoToString(cpu_topo) << std::endl;

    perf.resume();
    auto start = std::chrono::high_resolution_clock::now();
    
    size_t total_cpus = getCpuCount(cpu_topo);
    omp_set_num_threads(total_cpus); 


    // std::vector<size_t> thread_pin_map = flat_thread_pin_map(cpu_topo);

    size_t groups = cpu_topo.size();

    // int tid = omp_get_thread_num();
    // size_t core = thread_pin_map[tid];
    // size_t node = node_of_thread_from_map(cpu_topo, thread_pin_map, tid);
    // set_thread_affinity(core);

    std::cout << "Total CPUs: " << total_cpus << ", Groups: " << groups << std::endl;


    std::vector<CoresetChannel*> channels(groups);
    size_t features, coreset_size;

    double group_max_work = 0.0;
    #pragma omp parallel num_threads(groups) reduction(max: group_max_work)
    
    {
        int group_id = omp_get_thread_num();
        int group_cpus = total_cpus / groups;

        omp_set_nested(1);


        if (group_id == 0) {
            MemoryStreamChannel* ch = new MemoryStreamChannel(argc, argv);
            features = ch->get_features();
            coreset_size = ch->get_coreset_size();
            channels[group_id] = ch;
        } else {
            channels[group_id] = new ThreadSafeQueue();
        }

        constexpr size_t MAX_BUCKETS = 20;
        std::array<std::vector<std::vector<float>>, MAX_BUCKETS> buckets;
        std::array<omp_lock_t, MAX_BUCKETS> bucket_mutexes;
        for (size_t i = 0; i < MAX_BUCKETS; ++i) {
            omp_init_lock(&bucket_mutexes[i]);
        }

        #pragma omp barrier

        #pragma omp parallel num_threads(group_cpus) reduction(max: group_max_work)
        {

        int local_tid = omp_get_thread_num();

        size_t cpu_id = 0;
        {
            size_t core = 0;
            size_t core_offs = 0;
            CpuSet *cores = &cpu_topo[group_id].cores[core];
            while (static_cast<size_t>(local_tid) >= cores->size() + core_offs) {
                core_offs += cores->size();
                cores = &cpu_topo[group_id].cores[++core];
            }
    
            cpu_id = (*cores)[local_tid - core_offs];
        }

        set_thread_affinity(cpu_id);

        bool isSink = (group_id == groups - 1);

        CoresetChannel& in_ch = *channels[group_id];
        CoresetChannel& out_ch = isSink ? in_ch : *channels[group_id + 1];
        
        double t0 = omp_get_wtime();
        while (true) {
            int task_rank = -1;
            std::vector<float> c1, c2;

            if (isSink) {
                for (int rank = buckets.size() - 1; rank >= 0; --rank) {
                    omp_set_lock(&bucket_mutexes[rank]);
                    if (buckets[rank].size() >= 2) {
                        c1 = std::move(buckets[rank].back());
                        buckets[rank].pop_back();
                        c2 = std::move(buckets[rank].back());
                        buckets[rank].pop_back();
                        task_rank = rank;
                    }
                    
                    omp_unset_lock(&bucket_mutexes[rank]);
                    if (task_rank != -1) break;
                }
            }

            if (task_rank == -1) {
                c1 = in_ch.pop();
                if (c1.empty()) break; // Exit if no more data
            }

            // --- Phase 3: Execute the work (NO LOCKS HELD) ---
            std::vector<float> result_coreset;
            if (task_rank == -1) { // New batch from stream
                result_coreset = Coreset<size_t, false, 3, 3U>(
                    c1.data(), c1.size() / features,
                    features, coreset_size
                );
            } else { // Merging two existing coresets
                c1.insert(c1.end(), c2.begin(), c2.end());
                result_coreset = Coreset<size_t, true, 3, 3U>(
                    c1.data(), c1.size() / (features + 1),
                    features, coreset_size
                );
            }

            size_t result_rank = (task_rank == -1) ? 0 : task_rank + 1;
            fassert(result_rank < MAX_BUCKETS, "Result rank exceeds bucket size");
            
            if (isSink) {
                omp_set_lock(&bucket_mutexes[result_rank]);
                buckets[result_rank].push_back(std::move(result_coreset));
                omp_unset_lock(&bucket_mutexes[result_rank]);
            } else {
                out_ch.push(std::move(result_coreset));
            }

        } // End of while loop
        double t1 = omp_get_wtime();
        group_max_work =  t1 - t0;

        } // End of inner parallel region

        std::cout << "Group " << group_id << " finished processing." 
                  << " Max work time: " << group_max_work << " s" << std::endl;

        bool isSink = (group_id == groups - 1);
        delete channels[group_id];
        channels[group_id] = nullptr;
        if (!isSink) {
            // Signal the next channel to stop
            std::cout << "Signaling next channel to stop." << std::endl;\
            for (int i = 0; i < group_cpus; ++i) {
                channels[group_id + 1]->push({});
            }
            std::cout << "Next channel signaled." << std::endl;
        } else {
            for (int rank = 2; rank < buckets.size(); ++rank) {
                if (buckets[rank].empty()) continue;
                buckets[1].insert(buckets[1].end(), std::make_move_iterator(buckets[rank].begin()), std::make_move_iterator(buckets[rank].end()));
                buckets[rank].clear();
            }
        }

    } // End of outer parallel region

    std::cout << "Group 0 pure‐work time (excl. affinity): "  << group_max_work << " s\n";

    //std::cout << "Final bucket sizes:";
    //for (const auto& rank : buckets) {
      	//std::cout << " ," << rank.size();
    //}
    //std::cout << std::endl;

    // // reduce all coresets in buckets[1]
    // for (int rank = 2; rank < buckets.size(); ++rank) {
    //     if (buckets[rank].empty()) continue;
    //     buckets[1].insert(buckets[1].end(), std::make_move_iterator(buckets[rank].begin()), std::make_move_iterator(buckets[rank].end()));
    //     buckets[rank].clear();
    // }


    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    perf.pause();
    std::cout << "Coreset computed in " << duration << " ms" << std::endl;

    return 0;
}
