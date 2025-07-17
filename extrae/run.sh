
source /usr/etc/extrae.sh

# mpirun --oversubscribe -np 10 ./trace.sh ../build/mpi-minimal
mpirun -n 7 ./trace.sh ../build/mpi-v2-perf ../.data/blobs3/data.bin out

/usr/bin/mpi2prv -f TRACE.mpits -o output.prv