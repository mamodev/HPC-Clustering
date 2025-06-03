
python3 gen.py --outdir .data/blobs1 blobs --n_samples 10000 --n_features 256  --n_clusters 3


python3 gen.py --outdir .data/blobs2 blobs --n_samples 1000000 --n_features 256  --n_clusters 3

python3 gen.py --outdir .data/blobs3 blobs --n_samples 10000000 --n_features 256  --n_clusters 3


python3 gen.py --outdir .data/blobs3 blobs --n_samples 1000000 --n_features 64  --n_clusters 3


<!-- this is too diff -->
python3 gen.py --outdir .data/class1 classification --n_samples 10000 --n_features 256 --n_classes 3 --n_informative 10 --n_redundant 5 