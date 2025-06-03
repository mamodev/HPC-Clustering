import struct
import os
import numpy as np
import matplotlib.pyplot as plt

plt.style.use('dark_background')

data_dir = ".data/class1"
output_dir = ".outs/serial-v1"

with open(os.path.join(data_dir, "data.bin"), "rb") as f:
    # format: I (dims), dims * I (shape), numpy array of float32
    dims = struct.unpack("I", f.read(4))[0]
    shape = struct.unpack("I" * dims
                        , f.read(4 * dims))
    X = np.frombuffer(f.read(), dtype=np.float32)
    X = X.reshape(shape)


with open(os.path.join(data_dir, "labels.bin"), "rb") as f:
    # format: Q (nlables), uint64 * nlables (nupy array of uint64)
    nlabels = struct.unpack("Q", f.read(8))[0]
    Y = np.frombuffer(f.read(), dtype=np.uint64)


# list all files that ends with result.bin in the data_dir
result_files = [f for f in os.listdir(output_dir) if f.endswith("result.bin")]
ZZ = []
for result_file in result_files:
    with open(os.path.join(output_dir, result_file), "rb") as f:
        # format: Q (nresults), uint64 * nresults (numpy array of uint64)
        nresults = struct.unpack("Q", f.read(8))[0]
        Z = np.frombuffer(f.read(), dtype=np.uint64)
        ZZ.append(Z)
        assert len(Z) == len(X), f"Z has different length than X: {len(Z)} != {len(X)}"

assert len(ZZ) == len(result_files), f"ZZ has different length than result_files: {len(ZZ)} != {len(result_files)}"


# create 1 + len(ZZ) subplots.
# Where the point is colored by the value of Y in the first subplot
# and the point is colored by the value of Z in the rest of the subplots

def plot(X, Y, ZZ):

    # create subplots
    MAX_PLOT_PER_ROW = 3
    NPLOTS = len(ZZ) + 1
    NROWS = (NPLOTS - 1) // MAX_PLOT_PER_ROW + 1
    NCOLS = min(MAX_PLOT_PER_ROW, NPLOTS)
    projection = '3d' if len(X[0]) >= 3 else '2d'    

    print(f"NROWS: {NROWS}, NCOLS: {NCOLS}, projection: {projection}")
    print(f"len(ZZ): {len(ZZ)}")

    fig = plt.figure()
    ax = fig.add_subplot(NROWS, NCOLS, 1, projection=projection)
    if projection == '3d':
        if len(X[0]) > 3:
            from sklearn.decomposition import PCA
            pca = PCA(n_components=3)
            X = pca.fit_transform(X)


        
        ax.scatter(X[:, 0], X[:, 1], X[:, 2], c=Y, cmap='viridis', marker='o')
        ax.set_xlabel('Feature 1')
        ax.set_ylabel('Feature 2')
        ax.set_zlabel('Feature 3')
    else:
        ax.scatter(X[:, 0], X[:, 1], c=Y, cmap='viridis', marker='o')
        ax.set_xlabel('Feature 1')
        ax.set_ylabel('Feature 2')

    for i, Z in enumerate(ZZ):
        ax = fig.add_subplot(NROWS, NCOLS, i + 2, projection=projection)
        if projection == '3d':
            ax.scatter(X[:, 0], X[:, 1], X[:, 2], c=Z, cmap='viridis', marker='o')
            ax.set_xlabel('Feature 1')
            ax.set_ylabel('Feature 2')
            ax.set_zlabel('Feature 3')
            ax.set_title(result_files[i])
        else:
            ax.scatter(X[:, 0], X[:, 1], c=Z, cmap='viridis', marker='o')
            ax.set_xlabel('Feature 1')
            ax.set_ylabel('Feature 2')
            ax.set_title(result_files[i])


    plt.tight_layout()
    plt.show()


# plot(X, Y, ZZ)


# Compare with sklearn algorithms
from sklearn.cluster import MiniBatchKMeans


# create a kmeans model
kmeans = MiniBatchKMeans(n_clusters=3, batch_size=1000, random_state=42)
kmeans.fit(X)
Z = kmeans.predict(X)

result_files = ["sklearn_kmeans"] + result_files
ZZ = [Z] + ZZ


# calc accuracy
for i, Z in enumerate(ZZ):
    real_label_map = {}
    correct = 0 
    for j in range(len(Z)):
        if Z[j] not in real_label_map:
            real_label_map[Z[j]] = Y[j]

        if real_label_map[Z[j]] == Y[j]:
            correct += 1

    print(f"Accuracy for {result_files[i]}: {correct / len(Z)}")


       