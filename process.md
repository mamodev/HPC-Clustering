# Optimization process

This file will describe the wole process of optimization for the algorithm in an incremental way.

## Naive implementation
todo: describe implementation of serial-v1 algorithm

## OMP parallelization

#### step 1: Profile and find bottleneck

In order to have a good profiling, we need to generate datasets with either high and low dimensionality.

also we need to run the algorithm with different parameters:
- number of restarts
- coreset size
- kmeans settings...

so we generate two datasets:
- `blobs1` with 3 dimensions
- `blobs2` with 512 dimensions

```
python3 gen.py --outdir .data/blobs1 blobs --n_samples 10000000 --n_features 3 --n_clusters 8

python3 gen.py --outdir .data/blobs2 blobs --n_samples 60000 --n_features 512 --n_clusters 8
```

we use 10000000 for 3 dims and 60000 for 512 dims in order too keep amount of data similar.

> we keep coreset size to 10% of the dataset size. so (for 3 dims) 1000000 and (for 512 dims) 6000.

we use taskset to pin the process to a single core, so we can profile it better (removing the noise from other processes and NUMA effects).

```
 Performance counter stats for 'taskset -c 5 ./build/serial-v1 .data/blobs2/data.bin .outs/serial-v1/blobs2':

         17,330.84 msec task-clock                       #    0.771 CPUs utilized             
    75,237,210,581      cycles                           #    4.341 GHz                         (75.00%)
    71,064,052,216      instructions                     #    0.94  insn per cycle              (74.99%)
    38,818,158,573      L1-dcache-loads                  #    2.240 G/sec                       (74.99%)
     5,737,762,840      L1-dcache-load-misses            #   14.78% of all L1-dcache accesses   (74.99%)
     1,033,441,619      L1-icache-loads                  #   59.630 M/sec                       (75.00%)
        10,217,847      L1-icache-load-misses            #    0.99% of all L1-icache accesses   (75.01%)
     3,061,762,384      branches                         #  176.666 M/sec                       (75.00%)
        24,131,602      branch-misses                    #    0.79% of all branches             (75.01%)

      22.489397517 seconds time elapsed
```

From the profiling we can see that we achive poor instruction per cycle (IPC) of 0.94, which is very low. We also see that L1 cache miss rate is high (14.78%), which means we are not using the cache efficiently.

From perf report we can see that the 52% of time is spent in 
``
53.53%    52.04%  serial-v1-perf  serial-v1-perf        [.] unsigned long ct_node::pick_centroid<1>()
21.36%    20.74%  serial-v1-perf  serial-v1-perf        [.] indices::split(unsigned long, unsigned long, flat_points) 
```

In particular is spended in distance computation, which is the most expensive operation in the algorithm.



The data flow is:
points[m*2] => CoresetTree => Coreset => CoresetReduction


where CoresetTree = O(d * n * log m) where n = 2*m so => O(d * m)

where CoresetReduction 


