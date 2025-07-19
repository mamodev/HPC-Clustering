#include <omp.h>
#include <iostream>
#include <deque>
#include <vector>
#include <mutex>
#include <cassert>
#include <thread>
#include "parser.hpp"
#include "coreset.hpp"


// A dynamically‐resizable array of buckets/locks/presence flags:
static std::deque<std::vector<float>> buckets;
static std::deque<std::mutex>        bucket_mutexes;
static std::deque<bool>              bucket_filled;
static std::mutex                     resize_mutex;

// Ensure we have room for rank R
static void ensure_rank(size_t R) {
    std::lock_guard<std::mutex> g(resize_mutex);
    if (R >= buckets.size()) {
        buckets.resize(R+1);
        bucket_filled.resize(R+1,false);
        bucket_mutexes.resize(R+1);  // now valid: deque never moves its existing mutexes
    }
}

// The recursive chain‐merge function.
// Called inside an OpenMP task.
static void merge_chain(size_t rank,
                        std::vector<float> cs,
                        size_t features,
                        size_t coreset_size)
{
  // Make sure the data structures are big enough:
  ensure_rank(rank);

  // Try to insert into buckets[rank].  If empty, done.
  {
    std::lock_guard<std::mutex> g(bucket_mutexes[rank]);
    if (!bucket_filled[rank]) {
      buckets[rank] = std::move(cs);
      bucket_filled[rank] = true;
      return;
    }
  }

  // Otherwise we have a collision: pull the old one out, clear slot,
  // then spawn a task to merge them at rank+1.
  std::vector<float> old_cs;
  {
    std::lock_guard<std::mutex> g(bucket_mutexes[rank]);
    old_cs = std::move(buckets[rank]);
    bucket_filled[rank] = false;
  }

  // Spawn a new task that merges cs + old_cs → new_cs at rank+1
  #pragma omp task firstprivate(rank,cs,old_cs) \
                   depend(inout: buckets, bucket_filled)
  {
    // concatenate raw data
    std::vector<float> concat;
    concat.reserve(cs.size() + old_cs.size());
    concat.insert(concat.end(), cs.begin(), cs.end());
    concat.insert(concat.end(), old_cs.begin(), old_cs.end());

    // weighted coreset constructor:
    auto new_cs = Coreset<size_t, true, 3, 3U>(
      concat.data(),
      concat.size() / (features+1),
      features,
      coreset_size
    );
    // recurse one level up
    merge_chain(rank+1, std::move(new_cs), features, coreset_size);
  }
}

int main(int argc, char* argv[]) {
  MemoryStream stream(argc, argv);
  const size_t coreset_size = stream.coreset_size;
  const size_t features     = stream.features;

  // Pre‐allocate a few ranks to avoid repeated resizing:
  ensure_rank(8);

  // Use half the hardware threads (no hyperthreading):
  omp_set_num_threads(
    std::max(1u, std::thread::hardware_concurrency()/2u)
  );

  #pragma omp parallel
  #pragma omp single
  {
    // 1) Streaming loop: pull batches, spawn a task to build rank‐0 coreset
    while (true) {
      auto batch = stream.next_batch();
      if (batch.empty()) break;

      #pragma omp task firstprivate(batch) depend(inout: buckets)
      {
        // unweighted coreset of a raw batch:
        auto cs0 = Coreset<size_t, false, 3, 3U>(
          batch.data(),
          batch.size()/features,
          features,
          coreset_size
        );
        // insert into rank 0, possibly spawning merge tasks:
        merge_chain(0, std::move(cs0), features, coreset_size);
      }
    }

    // 2) Wait for all build+merge tasks to finish
    #pragma omp taskwait

    // 3) Final flush: collect whatever is left in the buckets
    std::vector<std::vector<float>> leftovers;
    {
      // Protect reads of bucket_filled/buckets
      std::lock_guard<std::mutex> g(resize_mutex);
      for (size_t r = 0; r < buckets.size(); ++r) {
        if (bucket_filled[r]) {
          leftovers.emplace_back(std::move(buckets[r]));
          bucket_filled[r] = false;
        }
      }
    }

    // 4) Reduce any remaining coresets in a simple loop
    if (leftovers.empty()) {
      std::cerr << "No data processed!\n";
      std::exit(1);
    }
    auto final_cs = std::move(leftovers[0]);
    for (size_t i = 1; i < leftovers.size(); ++i) {
      // concat and re‐coreset
      auto &B = leftovers[i];
      std::vector<float> concat;
      concat.reserve(final_cs.size() + B.size());
      concat.insert(concat.end(), final_cs.begin(), final_cs.end());
      concat.insert(concat.end(), B.begin(), B.end());

      final_cs = Coreset<size_t, true, 3, 3U>(
        concat.data(),
        concat.size()/(features+1),
        features,
        coreset_size
      );
    }

    std::cout << "Final coreset size: " << final_cs.size() << "\n";
  } // end parallel

  return 0;
}