#include <stdio.h>
#include <stdlib.h>
#include <time.h>

// #define L1_CACHE_SIZE (32 * 1024) // Example: 32KB
// 256kb
#define L1_CACHE_SIZE (256 * 1024) // Example: 256KB
#define NUM_ITERATIONS 1000000

int main() {
    char *data = (char *)malloc(L1_CACHE_SIZE);
    if (data == NULL) {
        perror("Memory allocation failed");
        return 1;
    }

    // Initialize data (optional, but can help avoid first-touch effects)
    for (int i = 0; i < L1_CACHE_SIZE; i++) {
        data[i] = (char)i;
    }

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    // Sequential Access
    for (int i = 0; i < NUM_ITERATIONS; i++) {
        for (int j = 0; j < L1_CACHE_SIZE; j++) {
            volatile char temp = data[j]; // Volatile to prevent compiler optimization
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    long seconds = end.tv_sec - start.tv_sec;
    long nanoseconds = end.tv_nsec - start.tv_nsec;
    double elapsed_time = seconds + nanoseconds * 1e-9;

    double bytes_accessed = (double)NUM_ITERATIONS * L1_CACHE_SIZE;
    double bandwidth = bytes_accessed / elapsed_time;

    printf("L1 Bandwidth (Sequential): %.2f bytes/second\n", bandwidth);

    free(data);
}
  
