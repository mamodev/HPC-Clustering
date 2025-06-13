#include "CoresetTree.hpp"
#include "CoresetStream.hpp"
#include "Kmeans.hpp"
#include "parser.hpp"
#include "perf.hpp"


#if !defined(CLUSTERS)
#define CLUSTERS 3
#endif

#if !defined(BATCH_SIZE)
#define BATCH_SIZE 1000
#endif

#if !defined(BATCH_ROUNDS)
#define BATCH_ROUNDS 3
#endif

int main(int argc, char **argv) {
    auto perf = PerfManager();
    auto deltas = DeltaTimer();
    perf.pause();

    auto [samples, outPath, coresetSize] = parseArgs<float>(argc, argv);
    // std::vector<int> labels(samples.samples, -1);
    // CoresetStream coreset_stream(CORESET_SIZE, samples.features);

    perf.resume();

    auto root = CoresetTree(samples.data.data(),  nullptr, samples.samples, samples.features, coresetSize);

        
    // auto stop_insert = deltas.start("insert");
    
    // for (size_t i = 0; i < samples.samples; ++i) {
    //     coreset_stream.insert(samples.data.data() + i * samples.features);
    // }

    // stop_insert();

    // auto stop_final = deltas.start("final-coreset");
    // Coreset coreset = coreset_stream.final_coreset();
    // stop_final();

    // auto stop_kmeans = deltas.start("kmeans");
    // auto centroids = kmeans(coreset, CLUSTERS, BATCH_ROUNDS, BATCH_SIZE);
    // stop_kmeans();

    // auto stop_lable_assignment = deltas.start("label-assignment");
    // for (size_t i = 0; i < samples.samples; ++i) {
    //     float min_dist = std::numeric_limits<float>::max();
    //     int best_cluster = -1;
    //     for (size_t j = 0; j < CLUSTERS; ++j) {
    //         const float* centroid = centroids.data() + j * samples.features;

    //         float dist = 0.0;
    //         for (size_t d = 0; d < samples.features; ++d) {
    //             float diff = samples.data[i * samples.features + d] - centroid[d];
    //             dist += diff * diff;
    //         }

    //         if (dist < min_dist) {
    //             min_dist = dist;
    //             best_cluster = j;
    //         }
    //     }

    //     assert(best_cluster != -1 && "Best cluster is -1!");
    //     labels[i] = best_cluster;
    // }
    // stop_lable_assignment();

    perf.pause();



    // deltas.to_file(outPath + "/deltas.csv");
    // writeResults(outPath + "/result.bin", labels);

    // std::string filename = outPath + "/coreset_" + std::to_string(CORESET_SIZE) + ".csv";
    // std::ofstream
    // file(filename);
    // if (file.is_open()) {
    //     file << coreset.csv();
    //     file.close();
    // } else {
    //     std::cerr << "Unable to open file: " << filename << std::endl;
    // }

    // std::cout << "Coreset: n = " << coreset.points.n << ", d = " << coreset.points.d << "\n";
}