#pragma once
#include <thread>
#include <iostream>
#include <vector>
#include <mutex>
#include <condition_variable>

#include "assert.hpp"
#include "parser.hpp"
#include "coreset.hpp"

class Channel {
    enum class WorkerStatus {
        Idle,
        Working,
        Done
    };
    
    private:
      std::mutex mtx_;
      std::condition_variable cv_;
      std::condition_variable* global_cv_ = nullptr;
      int tag_ = -1; // -1 means no work
      std::vector<float> c1_, c2_, res_;
      WorkerStatus status_ = WorkerStatus::Idle;
    
    public:
        Channel() = default;
        Channel(std::condition_variable* global_cv) : global_cv_(global_cv) {}
    
        Channel(const Channel&) = delete;
        Channel& operator=(const Channel&) = delete;
        
        Channel(Channel&&) = default;
        Channel& operator=(Channel&&) = default;
    
        bool isReady() {
            std::lock_guard<std::mutex> lock(mtx_);
            return status_ == WorkerStatus::Idle;
        }
    
        bool isDone() {
            std::lock_guard<std::mutex> lock(mtx_);
            return status_ == WorkerStatus::Done;
        }
    
        std::tuple<int, std::vector<float>> getResult() {
            std::lock_guard<std::mutex> lock(mtx_);
            fassert(status_ == WorkerStatus::Done, "Channel is not in done state");
    
            status_ = WorkerStatus::Idle; // Reset status to Idle after getting result
    
            int tmp_tag = tag_;
            tag_ = -1; // Reset tag to -1 after getting result
            
            return {tmp_tag, std::move(res_)};
        }
    
        void postWork(int tag,  std::vector<float> c1,  std::vector<float> c2) {
            fassert(tag < 0 || !c1.empty(), "The first vector must not be empty");
    
            std::unique_lock<std::mutex> lock(mtx_);
            fassert(status_ == WorkerStatus::Idle, "Channel is not ready for new work");
            tag_ = tag;
            c1_ = std::move(c1);
            c2_ = std::move(c2);
            status_ = WorkerStatus::Working;
            cv_.notify_one(); // Notify the worker thread
        }
    
        void workDone(std::vector<float> res) {
            // std::cout << "Work done with tag: " << tag_ << ", result size: " << res.size() << std::endl;
    
            fassert(!res.empty(), "Result vector must not be empty");
    
            std::lock_guard<std::mutex> lock(mtx_);
            
            fassert(status_ == WorkerStatus::Working, "Channel is not in working state");
    
            res_ = std::move(res);
            status_ = WorkerStatus::Done;
    
            if (global_cv_) {
                global_cv_->notify_all(); // Notify the global condition variable
            }
        }
    
        // return tag and merged vector
        std::tuple<int, std::vector<float>> getWork() {
    
            std::unique_lock<std::mutex> lock(mtx_);
            if (status_ != WorkerStatus::Working) {
                cv_.wait(lock, [this] { return status_ == WorkerStatus::Working; });
            }
    
            fassert(status_ == WorkerStatus::Working, "Channel is not in working state");
    
            // check if both vectors are not empty if not merge them
            if (c2_.empty()) {
                return {tag_, std::move(c1_)};
            }
    
            fassert(!c1_.empty(), "First vector must not be empty");
    
            c1_.insert(c1_.end(), c2_.begin(), c2_.end());
            c2_.clear(); // Clear the second vector after merging
    
            return {tag_, std::move(c1_)};
        }
    };

template<typename int_t, int MinSplitIters, unsigned int Seed>
void worker(Channel &channel, size_t coreset_size, size_t features) {
    while(true) {
        auto [tag, batch] = channel.getWork();
        if (tag == -1) {
            // std::cout << "Worker received no work, exiting." << std::endl;
            break;
        }   

        if (tag == 0) {
            auto coreset = Coreset<int_t, false, MinSplitIters, Seed>(batch.data(), batch.size() / features, features, coreset_size);
            channel.workDone(std::move(coreset));
        } else {
            auto coreset = Coreset<int_t, true, MinSplitIters, Seed>(batch.data(), batch.size() / (features + 1), features, coreset_size);
            channel.workDone(std::move(coreset));
        }
    }
}

void set_affinity(std::thread& t, int core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);

    int rc = pthread_setaffinity_np(
        t.native_handle(),       // POSIX handle of the std::thread
        sizeof(cpu_set_t),
        &cpuset
    );
    
    if (rc != 0) {
        std::cerr << "Error calling pthread_setaffinity_np: " << rc << "\n";
    }
}

template<typename int_t = std::size_t, int MinSplitIters=3, unsigned int Seed = 0U>
std::vector<float> coresetStreamTnaive(CoresetStream& stream) {

    std::condition_variable global_cv;

    int nworkers = std::thread::hardware_concurrency() / 2; // NO HYPERTHREADING (COMPUTE BOUND)
    std::vector<std::thread> workers;
    std::vector<std::unique_ptr<Channel>> channels;
    for (int i = 0; i < nworkers; ++i) {
        channels.emplace_back(std::make_unique<Channel>(&global_cv));
    }

    for (int i = 0; i < nworkers; ++i) {
        workers.emplace_back(worker<int_t, MinSplitIters, Seed>, std::ref(*channels[i]), stream.coreset_size, stream.features);
        set_affinity(workers.back(), i);
    }

    // Buckets[rank][i] => coreset of rank rank
    auto get_free_ch = [](std::vector<std::unique_ptr<Channel>>& channels, size_t& ready_count) -> Channel& {
        for (auto& channel_ptr : channels) {
            auto& channel = *channel_ptr;
            if (channel.isReady()) {
                ready_count--;
                return channel;
            }
        }
        std::terminate();
    };

    std::vector<std::vector<std::vector<float>>> buckets;
    size_t ready_count = 0;
    std::mutex lock;
    std::unique_lock<std::mutex> ulock(lock);
    auto batch = stream.next_batch();

    while(ready_count < nworkers) {

        ready_count = 0;
        while (ready_count == 0) {
            for (auto& channel_ptr : channels) {
                auto& channel = *channel_ptr;
                
                if (channel.isReady()) {
                    ++ready_count;
                } else if (channel.isDone()) {
                    auto [tag, res] = channel.getResult();
                    while (buckets.size() <= tag + 1)
                        buckets.emplace_back();

                    buckets[tag + 1].push_back(std::move(res));
                    ready_count++;
                }
            }
    
            if (ready_count == 0) 
                global_cv.wait(ulock, []() {return true; });
        }
    

        int rank = buckets.size() - 1;
        while (rank >= 0 && ready_count > 0) {
            auto& coresets = buckets[rank];
            int reductions = coresets.size() / 2;
            int perfomable_reductions = std::min(reductions, static_cast<int>(ready_count));
            for (int r = 0; r < perfomable_reductions; ++r) {
                auto& ch = get_free_ch(channels, ready_count);
                ch.postWork(rank, std::move(coresets.back()), std::move(coresets[coresets.size() - 2]));
                coresets.pop_back();
                coresets.pop_back();
            }
            rank--;
        }

        if (batch.empty() && ready_count == nworkers && buckets.size() > 2) {
            for (int rank = 2; rank < buckets.size(); ++rank) {
                if (buckets[rank].empty()) continue;
                buckets[1].insert(buckets[1].end(), std::make_move_iterator(buckets[rank].begin()), std::make_move_iterator(buckets[rank].end()));
                buckets[rank].clear();
            }

            buckets.resize(2);
            ready_count = 0; // make another iteration to process the remaining coresets
        }
    

        if (rank < 0 && ready_count != 0 && !batch.empty()) {
            auto& ch = get_free_ch(channels, ready_count);
            ch.postWork(0, std::move(batch), std::vector<float>());
            batch = stream.next_batch();
        }
    }

    for (int i = 0; i < nworkers; ++i) {
        auto& channel = *channels[i];
        fassert(channel.isReady(), "Channel should be ready at this point");
        channel.postWork(-1, std::vector<float>(), std::vector<float>());
    }


    for (auto &worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    // there should be only one bucket with size = 1
    size_t total_size = 0;
    for (auto& bucket : buckets) {
        total_size += bucket.size();
    }

    fassert(total_size == 1, "There should be only one bucket with size = 1");

    for (auto& bucket : buckets) {
        if (bucket.size() == 1) {
            return std::move(bucket[0]);
        }
    }

    // unreachable code, but just in case
    std::terminate();
}

    