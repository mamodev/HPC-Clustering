import os
import struct
import numpy as np
import matplotlib.pyplot as plt


FILE = '.outs/out/coreset.bin'
DATASET = '.data/blobs104/data.bin'


def read_samples(path: str, dtype=np.float32) -> dict:
    print(f"Reading samples from: {path}")

    with open(path, 'rb') as f:
        try:
            dims_bytes = f.read(4)
            if not dims_bytes:
                raise IOError("Failed to read dims (file might be empty or too short)")
            dims = struct.unpack('<I', dims_bytes)[0]
        except struct.error as e:
            raise IOError(f"Failed to unpack dims: {e}")

        try:
            shape_bytes = f.read(dims * 4)
            if len(shape_bytes) != dims * 4:
                raise IOError(f"Failed to read full shape (expected {dims * 4} bytes, got {len(shape_bytes)})")
            shape = struct.unpack(f'<{dims}I', shape_bytes)
        except struct.error as e:
            raise IOError(f"Failed to unpack shape: {e}")

        n = shape[0]

        features = 1
        if dims > 1:
            for i in range(1, dims):
                features *= shape[i]

        expected_elements = n * features

        if struct.calcsize('f') != np.dtype(dtype).itemsize:
            raise ValueError(
                f"Mismatch in data type size. C++ code uses `float` (4 bytes), "
                f"but provided `dtype` ({dtype}) has itemsize {np.dtype(dtype).itemsize} bytes. "
                f"Consider using `dtype=np.float32`."
            )

        data_size_bytes = expected_elements * np.dtype(dtype).itemsize
        data_bytes = f.read(data_size_bytes)
        if len(data_bytes) != data_size_bytes:
            raise IOError(f"Failed to read all sample data (expected {data_size_bytes} bytes, got {len(data_bytes)})")

        data = np.frombuffer(data_bytes, dtype=dtype).reshape(shape)

    return {
        'samples': n,
        'features': features,
        'data': data
    }


def read_coreset(filepath: str, dtype=np.float32) -> dict:
    print(f"Reading coreset from: {filepath}")

    with open(filepath, 'rb') as f:
        buff = f.read()

    # header: uint32_t coreset_size, uint32_t features
    if len(buff) < 8:
        raise IOError(f"File {filepath} is too short to contain header (expected at least 8 bytes, got {len(buff)})")
    
    coreset_size, nfeatures = struct.unpack('<II', buff[:8])

    # data layout: [weight, x1, x2, ..., xn]
    expected_size = coreset_size * (nfeatures + 1) * np.dtype(dtype).itemsize
    if len(buff) < 8 + expected_size:
        raise IOError(f"File {filepath} is too short to contain all coreset data (expected at least {8 + expected_size} bytes, got {len(buff)})")   
    
    data = np.frombuffer(buff[8:], dtype=dtype).reshape((coreset_size, nfeatures + 1))
    weights = data[:, 0]  # first column is weights
    features = data[:, 1:]  # remaining columns are features

    features = features.astype(dtype).reshape((coreset_size, nfeatures))

    return {
        'weights': weights,
        'features': features
    }

ds = read_samples(DATASET, dtype=np.float32)
cs = read_coreset(FILE, dtype=np.float32)
print(f"Dataset: {DATASET}, Samples: {ds['samples']}, Features: {ds['features']}")
print(f"Coreset: {FILE}, Points: {cs['features'].shape[0]}, Features: {cs['features'].shape[1]}")

ds_var = [ np.var(ds['data'][:, d]) for d in range(ds['features']) ]
cs_var = [ np.var(cs['features'][:, d]) for d in range(cs['features'].shape[1]) ]
cs_mean = [ np.mean(cs['features'][:, d]) for d in range(cs['features'].shape[1]) ]

print(f"Dataset Variance: {sum(ds_var)}")
print(f"Coreset Variance: {sum(cs_var)}")
print(f"Coreset Mean: {cs_mean}")

# Create two figures: on the left the dataset, on the right the coreset
# use PCA to reduce the dataset to 3D 
# in order to make pc not explode randomly sample PLT_NUM_P at max points from the dataset and coreset
PLT_NUM_P = 2000

from sklearn.decomposition import PCA

# randomly sample PLT_NUM_P points from the dataset
if ds['samples'] > PLT_NUM_P:
    idx = np.random.choice(ds['samples'], PLT_NUM_P, replace=False)
else:
    idx = np.arange(ds['samples'])

ds_sampled = ds['data'][idx, :]

if cs['weights'].shape[0] > PLT_NUM_P:
    idx = np.random.choice(cs['weights'].shape[0], PLT_NUM_P, replace=False)
else:
    idx = np.arange(cs['weights'].shape[0])

cs_sampled = cs['features'][idx, :]

# reduce to 3D

if ds_sampled.shape[1] > 3:
    pca = PCA(n_components=3)
    ds_sampled = pca.fit_transform(ds_sampled)
    cs_sampled = pca.transform(cs_sampled)


figs = plt.figure(figsize=(20, 10))
ax1 = figs.add_subplot(121, projection='3d')
ax2 = figs.add_subplot(122, projection='3d')

# plot dataset
ax1.scatter(ds_sampled[:, 0], ds_sampled[:, 1], ds_sampled[:, 2], s=4, alpha=0.5, color='blue', edgecolor='k', linewidth=0.1)
ax1.set_title('Dataset Points')
ax1.set_xlabel('X1')
ax1.set_ylabel('X2')
ax1.set_zlabel('X3')    

# plot coreset (weight rapresented as color interpolate from red to green 0 -> max weight)
print(len(cs_sampled[:, 0]))
ax2.scatter(cs_sampled[:, 0], cs_sampled[:, 1], cs_sampled[:, 2], s=4, alpha=0.8, edgecolor='k', linewidth=0.5)



ax2.set_title('Coreset Points')
ax2.set_xlabel('X1')
ax2.set_ylabel('X2')
ax2.set_zlabel('X3')

plt.show()








