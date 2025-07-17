#pragma once


// #define USE_PYTHON_PLOT


#if defined(USE_PYTHON_PLOT)

#include <pybind11/embed.h>
#include <pybind11/numpy.h>
namespace py = pybind11;



// Use this macro or define it explicitly for your build system
// For GCC/Clang:
#if defined(__GNUC__) || defined(__clang__)
#define CORESET_PLOTTER_LOCAL __attribute__((visibility("hidden")))
#else
#define CORESET_PLOTTER_LOCAL
#endif

class CORESET_PLOTTER_LOCAL CoresetPlotter {
private:
    py::scoped_interpreter guard{};
    py::module_ plot_module = py::module_::import("embed_plot");    
public:

    CoresetPlotter() {}

    void plot(const std::vector<float>& coreset, size_t features) {
        std::vector<py::ssize_t> shape = {static_cast<py::ssize_t>(coreset.size() / (features + 1)), static_cast<py::ssize_t>(features + 1)};

        py::array_t<float> coreset_array = py::array_t<float>(shape, coreset.data(), py::none());
    
        int result = plot_module.attr("plot")(1, coreset_array).cast<int>();
        if (result != 0) {
            std::cerr << "Plotting failed with error code: " << result << std::endl;
            throw std::runtime_error("Plotting failed");
        }
    }

    ~CoresetPlotter() {
        std::cout << "Plot window closing, press Enter to continue..." << std::endl;
        std::string response;
        std::getline(std::cin, response);
        plot_module.attr("exit")();
    }
};

#else

class CoresetPlotter {
public:
    CoresetPlotter() {}
    void plot(const std::vector<float>& coreset, size_t features) {
        auto pmean = std::vector<float>(features, 0.0f);
        auto pstd = std::vector<float>(features, 0.0f);
        size_t n = coreset.size() / (features + 1);
        std::cout << "Coreset size: " << n << std::endl;
        for (size_t i = 0; i < n; ++i) {
            for (size_t j = 0; j < features; ++j) {
                pmean[j] += coreset[i * (features + 1) + j + 1];
            }
        }

        for (size_t j = 0; j < features; ++j) {
            pmean[j] /= n;
        }

        for (size_t i = 0; i < n; ++i) {
            for (size_t j = 0; j < features; ++j) {
                float diff = coreset[i * (features + 1) + j + 1] - pmean[j];
                pstd[j] += diff * diff;
            }
        }   

        for (size_t j = 0; j < features; ++j) {
            pstd[j] = std::sqrt(pstd[j] / n);
        }

        std::cout << "Coreset mean: ";
        for (size_t j = 0; j < features; ++j) {
            std::cout << pmean[j] << " ";
        }
        std::cout << std::endl;

        std::cout << "Coreset std: ";
        for (size_t j = 0; j < features; ++j) {
            std::cout << pstd[j] << " ";        
        }
        std::cout << std::endl;
    }
};

#endif // USE_PYTHON_PLOT



// write coreset to file (coreset_size, features, samples)
// weight[0], f1[0], f2[0], ..., fN[0], weight[1], f1[1], f2[1], ..., fN[1], ...
// binary format
// std::ofstream outFile(outDir + "/coreset.bin", std::ios::binary);
// if (!outFile) {
//     std::cerr << "Error opening output file: " << outDir + "/coreset.bin" << std::endl;
//     return 1;
// }       


// uint32_t fcoreset_size = static_cast<uint32_t>(coreset_size);
// uint32_t features = static_cast<uint32_t>(samples.features);
// outFile.write(reinterpret_cast<const char*>(&fcoreset_size), sizeof(fcoreset_size));
// outFile.write(reinterpret_cast<const char*>(&features), sizeof(features));
// outFile.write(reinterpret_cast<const char*>(coreset.data()), coreset.size() * sizeof(float));
// outFile.close();

// std::cout << "Coreset written to: " << outDir + "/coreset.bin" << std::endl;


// int exit_code = system("python3 plot.py");
// if (exit_code != 0) {
//     std::cerr << "Error executing plot.py" << std::endl;
//     return 1;
// }

