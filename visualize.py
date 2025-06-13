import argparse
import matplotlib.pyplot as plt
import os

parser = argparse.ArgumentParser(description="Visualize dataset")
parser.add_argument(
    "path",
    type=str,
    help="Path to the dataset folder",
)

args = parser.parse_args()

if not os.path.exists(args.path):
    raise FileNotFoundError(f"Path {args.path} does not exist.")

if not os.path.isdir(args.path):
    raise NotADirectoryError(f"Path {args.path} is not a directory.")


labels_file = os.path.join(args.path, "labels.bin")
samples_file = os.path.join(args.path, "samples.bin")

if not os.path.exists(labels_file):
    raise FileNotFoundError(f"Labels file {labels_file} does not exist.")

if not os.path.exists(samples_file):
    raise FileNotFoundError(f"Samples file {samples_file} does not exist.")




