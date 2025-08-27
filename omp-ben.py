import subprocess
import re
import matplotlib.pyplot as plt
import pandas as pd
import os

def run_benchmark(num_threads, data_file, output_dir):
    print(f"Running with {num_threads}")
    command = [
        f"OMP_NUM_THREADS={num_threads}",
        "./build/omp-v1",
        data_file,
        output_dir,
    ]
    process = subprocess.Popen(
        " ".join(command), shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
    )
    stdout, stderr = process.communicate()
    if process.returncode != 0:
        print(f"Error for OMP_NUM_THREADS={num_threads}:\n{stderr}")
        return None
    match = re.search(r"Coreset computed in (\d+) ms", stdout)
    return int(match.group(1)) if match else None



def main():
    data_file = ".data/blobs007/data.bin"
    output_dir = ".tmp"
    benchmark_file = "benchmark_results.csv"

    if not os.path.exists("./build/omp-v1"):
        print("Error: ./build/omp-v1 not found.")
        return
    os.makedirs(output_dir, exist_ok=True)
    
    num_threads_list = [
        1, 2, 4, 8, 16, 32, 48, 64, 96, 128, 192, 256
    ]

    results = []

    for num_threads in num_threads_list:
        time_ms = run_benchmark(num_threads, data_file, output_dir)
        if time_ms is not None:
            results.append({"num_threads": num_threads, "time_ms": time_ms})

    if not results:
        print("No successful benchmark results.")
        return

    df = pd.DataFrame(results)
    df.to_csv(benchmark_file, index=False)
    print(f"Benchmark results saved to {benchmark_file}")

    plt.figure(figsize=(10, 6))
    plt.plot(df["num_threads"], df["time_ms"], marker="o", linestyle="-", color="skyblue")
    plt.title("Coreset Computation Time vs. Number of Threads")
    plt.xlabel("Number of OpenMP Threads")
    plt.ylabel("Coreset Computation Time (ms)")
    plt.grid(True)
    plt.xticks(num_threads_list)
    plt.tight_layout()
    plt.show()

if __name__ == "__main__":
    main()
