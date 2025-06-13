import numpy as np
from scipy.stats import multivariate_normal

def data_generator(
    num_clusters,
    total_points,
    dimensions,
    center_separation,
    cluster_spread_factor,
    batch_size, # The 'B' from your earlier prompt
    random_seed=42
):
    np.random.seed(random_seed)

    # 1. Pre-define cluster parameters (these will fit in RAM)
    centers = []
    cov_matrices = []

    for _ in range(num_clusters):
        center = np.random.uniform(low=-center_separation, high=center_separation, size=dimensions)
        centers.append(center)
        covariance = np.eye(dimensions) * cluster_spread_factor
        cov_matrices.append(covariance)

    # Convert to numpy arrays for faster indexing
    centers = np.array(centers)
    cov_matrices = np.array(cov_matrices)

    # Keep track of how many points generated so far
    generated_count = 0

    # 2. Yield batches of data and their labels
    while generated_count < total_points:
        # Determine how many points to generate in this batch
        points_to_generate_this_batch = min(batch_size, total_points - generated_count)

        if points_to_generate_this_batch == 0:
            break # No more points left to generate

        current_batch_points = np.empty((points_to_generate_this_batch, dimensions), dtype=np.float32) # Pre-allocate for data
        current_batch_labels = np.empty(points_to_generate_this_batch, dtype=np.int32) # Pre-allocate for labels

        # Decide which cluster each point in this batch comes from
        # This is more efficient than picking one by one
        cluster_indices_for_batch = np.random.randint(num_clusters, size=points_to_generate_this_batch)

        # Generate points for each cluster within the batch efficiently
        for cluster_idx in range(num_clusters):
            # Find all points in this batch that belong to the current cluster
            mask = (cluster_indices_for_batch == cluster_idx)
            num_points_in_this_sub_batch = np.sum(mask)

            if num_points_in_this_sub_batch > 0:
                # Generate all points for this specific cluster's subset at once
                sub_batch_points = multivariate_normal.rvs(
                    mean=centers[cluster_idx],
                    cov=cov_matrices[cluster_idx],
                    size=num_points_in_this_sub_batch
                )
                # Place them into the pre-allocated arrays at the correct positions
                current_batch_points[mask] = sub_batch_points
                current_batch_labels[mask] = cluster_idx # Assign the true cluster label

        # Yield both the data batch and its corresponding labels batch
        yield current_batch_points, current_batch_labels

        generated_count += points_to_generate_this_batch

    print(f"Finished generating {generated_count} points with labels.")


import argparse 
import struct
import os

parser = argparse.ArgumentParser(description='Generate data for testing.')
parser.add_argument('--num_clusters', type=int, default=3, help='Number of clusters to generate')
parser.add_argument('--total_points', type=int, default=1000, help='Total number of points to generate')
parser.add_argument('--dimensions', type=int, default=512, help='Number of dimensions for each point')
parser.add_argument('--center_separation', type=float, default=10.0, help='Separation between cluster centers')
parser.add_argument('--cluster_spread_factor', type=float, default=1.0, help='Spread factor for clusters')
parser.add_argument('--batch_size', type=int, default=50, help='Number of points to generate in each batch')
parser.add_argument('--seed', type=int, default=42, help='Random seed for reproducibility')
parser.add_argument('--outdir', type=str, help='Output directory for the data files', required=True)
args = parser.parse_args()

gen = data_generator(
    num_clusters=args.num_clusters,
    total_points=args.total_points,
    dimensions=args.dimensions,
    center_separation=args.center_separation,
    cluster_spread_factor=args.cluster_spread_factor,
    batch_size=args.batch_size,
    random_seed=args.seed
)

# with open(".data/gen2/data.bin", "wb") as f:

for i, (batch, lables) in enumerate(gen):
    print(f"Batch {i+1}: {batch.shape} points, Labels: {lables.shape}")
    # Here you can process the batch as needed
    # For example, you could save it to a file or feed it into a model
    # If you want to stop after a few batches, you can add a break condition
    if i >= 10:  # Just an example to limit output
        break



if not os.path.exists(args.outdir):
    os.makedirs(args.outdir)

with open(os.path.join(args.outdir, 'data.bin'), 'wb') as data_file:
    with open(os.path.join(args.outdir, 'labels.bin'), 'wb') as labels_file:
        for i, (X, Y) in enumerate(gen):
            if i == 0:
                data_file.write(struct.pack('III', 2, args.total_points, args.dimensions))
                labels_file.write(struct.pack('Q', args.total_points))

            # Write the data
            data_file.write(X.astype(np.float32).tobytes())
            labels_file.write(Y.astype(np.uint64).tobytes())

        