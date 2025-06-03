#usage ./run.sh <cmd> <ds>

./perf-mpi.sh $1 $2 3 --type=stat --type=mpip --type=record
./perf-mpi.sh $1 $2 5 --type=stat --type=mpip --type=record
./perf-mpi.sh $1 $2 7 --type=stat --type=mpip --type=record
./perf-mpi.sh $1 $2 9 --type=stat --type=mpip --type=record
./perf-mpi.sh $1 $2 11 --type=stat --type=mpip --type=record