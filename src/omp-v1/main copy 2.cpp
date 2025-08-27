#include "parser.hpp"
#include "coreset.hpp"
#include "topo.hpp"
#include "concurrentqueue.h"

#include <vector>

#include <barrier>
#include <thread>
#include <mutex>
#include <condition_variable>

#include <iostream>

class CoresetChannel {
public:
    virtual void push(const std::vector<float>& value) = 0;
    virtual std::vector<float> pop() = 0;
    virtual ~CoresetChannel() = default;
};

class MoodyCamelChannel : public CoresetChannel {
private:
    // Using a concurrent queue for vectors of floats
    moodycamel::ConcurrentQueue<std::vector<float>> queue_;

public:
    // Override push method.
    // ConcurrentQueue has an optimized enqueue method that handles moves.
    void push(const std::vector<float>& value) override {
        queue_.enqueue(value); // This will copy if you pass by const&
    }

    // You can also add a move-aware push for efficiency if the caller has an rvalue
    void push(std::vector<float>&& value) {
        queue_.enqueue(std::move(value)); // This will move
    }

    // Override pop method
    std::vector<float> pop() override {
        std::vector<float> value;
        // try_dequeue will return true if an item was successfully dequeued
        // and false if the queue was empty.
        while (!queue_.try_dequeue(value)) {
            // If the queue is empty, we might want to wait or yield.
            // For a blocking scenario like yours, you'd typically wait on a condition variable
            // or use a blocking variant (see blockingconcurrentqueue.h).
            // For this example, we'll just busy-wait briefly to show data flow.
            // In a real application, you might use std::this_thread::yield() or a proper blocking mechanism.
            // std::this_thread::sleep_for(std::chrono::microseconds(10)); 
        }
        return value;
    }

    // A non-blocking pop variant could be useful too
    bool try_pop(std::vector<float>& value) {
        return queue_.try_dequeue(value);
    }

    ~MoodyCamelChannel() override = default;
};

class ThreadSafeQueue : public CoresetChannel {
    private:
      struct Node {
        std::vector<float> data;
        std::unique_ptr<Node> next;
      };
    
      // head_ points at a dummy node; real data lives in head_->next
      std::unique_ptr<Node> head_;
      Node* tail_;  // only modified under tail_mutex_
    
      std::mutex head_mutex_;
      std::mutex tail_mutex_;
      std::condition_variable data_cond_;
    
      // Helper: acquire tail_ safely
      Node* get_tail() {
        std::lock_guard<std::mutex> lk(tail_mutex_);
        return tail_;
      }
    
      // Helper: pop the dummy head, waiting if queue is empty
      std::unique_ptr<Node> pop_head() {
        std::unique_lock<std::mutex> head_lk(head_mutex_);
        data_cond_.wait(head_lk, [&]{ return head_.get() != get_tail(); });
        auto old_head = std::move(head_);
        head_ = std::move(old_head->next);
        return old_head;
      }
    
    public:
      ThreadSafeQueue()
        : head_(std::make_unique<Node>())
        , tail_(head_.get())
      {}
    
      // Push as required by interface; amortizes to one copy into new node
      void push(const std::vector<float>& value) override {
        auto new_node = std::make_unique<Node>();
        new_node->data = value;  // one copy here
        Node* new_tail = new_node.get();
    
        {
          std::lock_guard<std::mutex> tail_lk(tail_mutex_);
          tail_->next = std::move(new_node);
          tail_ = new_tail;
        }
        data_cond_.notify_one();
      }
    
      // Pop as required by interface
      std::vector<float> pop() override {
        auto old_head = pop_head();
        return std::move(old_head->data);
      }
    
      ~ThreadSafeQueue() override = default;
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

#define MAX_BUCKETS 20

class GroupCtx {
public:
    int group_id;
    bool isSink;
    CoresetChannel *in_ch, *out_ch;
    std::array<std::vector<std::vector<float>>, MAX_BUCKETS> buckets;
    std::array<std::mutex, MAX_BUCKETS> bucket_mutexes;
};

void wmain(GroupCtx& ctx, int cpu, int features, size_t coreset_size) {
    if (cpu != -1) {
        set_thread_affinity(cpu);
    }
    while (true) {
        int task_rank = -1;
        std::vector<float> c1, c2;

        if (ctx.isSink) {
            for (int rank = ctx.buckets.size() - 1; rank >= 0; --rank) {
                std::lock_guard<std::mutex> lock(ctx.bucket_mutexes[rank]);
                if (ctx.buckets[rank].size() >= 2) {
                    c1 = std::move(ctx.buckets[rank].back());
                    ctx.buckets[rank].pop_back();
                    c2 = std::move(ctx.buckets[rank].back());
                    ctx.buckets[rank].pop_back();
                    task_rank = rank;
                }
                
                if (task_rank != -1) break;
            }
        }

        if (task_rank == -1) {
            c1 = ctx.in_ch->pop();
            if (c1.empty()) break; // Exit if no more data

            // copy c1 to new allocation in order to exploit NUMA locality
            auto c1_copy = std::vector<float>();
            c1_copy.reserve(c1.size());
            std::copy(c1.begin(), c1.end(), std::back_inserter(c1_copy));
            c1 = std::move(c1_copy);
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
        
        if (ctx.isSink) {
            std::lock_guard<std::mutex> lock(ctx.bucket_mutexes[result_rank]);
            ctx.buckets[result_rank].push_back(std::move(result_coreset));
        } else {
            fassert(ctx.out_ch != nullptr, "Output channel is null");
            ctx.out_ch->push(std::move(result_coreset));
        }

    } // End of while loop
}

int main(int argc, char* argv[]) {
    auto cpu_topo = detect_cpu_topology(true, true);
    std::cout << "Detected CPU Topology:\n" << cpuTopoToString(cpu_topo) << std::endl;

    int groups = cpu_topo.size();
    auto barrier = std::barrier<>(groups);
    std::vector<CoresetChannel*> channels(groups);

    size_t features, coreset_size;

    auto mthread = [&](int gid) {
        set_thread_affinity(cpu_topo[gid].cores[0][0]); // Pin thread to the first core of the group

        if (gid == 0) {
            MemoryStreamChannel* ch = new MemoryStreamChannel(argc, argv);
            features = ch->get_features();
            coreset_size = ch->get_coreset_size();
            channels[gid] = ch;
        } else {
            // channels[gid] = new ThreadSafeQueue();
            channels[gid] = new MoodyCamelChannel();
        }

        barrier.arrive_and_wait();
        auto start = std::chrono::high_resolution_clock::now();
  

        GroupCtx ctx;
        ctx.group_id = gid;
        ctx.isSink = (gid == groups - 1);
        ctx.in_ch = channels[gid];
        ctx.out_ch = ctx.isSink ? nullptr : channels[gid + 1];

        size_t nworkers = getNodeCoreCount(cpu_topo[gid]);
        auto workers = std::vector<std::thread>(nworkers - 1);
        for (int i = 0; i < nworkers - 1; ++i) {
            workers[i] = std::thread(wmain, std::ref(ctx), getNodeCpu(cpu_topo[gid], i + 1), features, coreset_size);
        }

        wmain(ctx, -1, features, coreset_size);

        for (auto& worker : workers) {
            worker.join();
        }

        delete ctx.in_ch; // Clean up channel
        ctx.in_ch = nullptr;

        if (ctx.isSink) {
            for (int rank = 2; rank < ctx.buckets.size(); ++rank) {
                if (ctx.buckets[rank].empty()) continue;
                ctx.buckets[1].insert(ctx.buckets[1].end(), std::make_move_iterator(ctx.buckets[rank].begin()), std::make_move_iterator(ctx.buckets[rank].end()));
                ctx.buckets[rank].clear();
            }
        } else {
            int next_gid = gid + 1;
            int next_g_nworkers = getNodeCoreCount(cpu_topo[next_gid]);
            for (int i = 0; i < next_g_nworkers; ++i){
                channels[next_gid]->push({});
            }
        }

        barrier.arrive_and_wait();
        if (gid == 0) {
            auto end = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> elapsed = end - start;
            std::cout << "Total processing time: " << elapsed.count() << " seconds" << std::endl;
        }
    };

    std::vector<std::thread> mthreads;
    for (int i = 0; i < groups; ++i) {
        mthreads.emplace_back(mthread, i);
    }

    for (auto& t : mthreads) {
        t.join();
    }

    std::cout << "All groups finished processing." << std::endl;
}
