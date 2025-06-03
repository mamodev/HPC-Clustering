#!/bin/bash
# MODIFIED PROFILING SCRIPT FOR MPIP
# Usage: ./perf-mpi.sh <cmd> <data_set> <num_processes> [...flags]
# where flags are:
#   --type=<type> (default: stat, options: stat, record, mpip)


CMD=$1
DATA_SET=$2
NUM_PROCESSES=$3
shift 3
FLAGS=("$@")

if [ ! -f ./build/$CMD-perf ]; then
    echo "Error: No executable found for $1-perf in ./build/"
    exit 1
fi

if [ ! -d .data/$DATA_SET ]; then
    echo "Error: No data set found for $2 in .data/"
    exit 1
fi

if ! [[ $NUM_PROCESSES =~ ^[0-9]+$ ]]; then
    echo "Error: Third argument must be a valid integer (number of processes)"
    exit 1
fi


TYPE_FLAGS=()
#filter all flags that start with --type=
for flag in "${FLAGS[@]}"; do
    if [[ $flag == --type=* ]]; then
        TYPE=${flag#--type=}
        TYPE_FLAGS+=("$TYPE")
    fi
done

if [ ${#TYPE_FLAGS[@]} -eq 0 ]; then
    TYPE_FLAGS=("stat")
fi


ENABLE_STAT="false"
ENABLE_RECORD="false"
ENABLE_MPIP="false"

if [[ " ${TYPE_FLAGS[*]} " == *" stat "* ]]; then
    ENABLE_STAT="true"
fi

if [[ " ${TYPE_FLAGS[*]} " == *" record "* ]]; then
    ENABLE_RECORD="true"
fi

if [[ " ${TYPE_FLAGS[*]} " == *" mpip "* ]]; then
    ENABLE_MPIP="true"
fi

echo "============================================================="
echo "Profiling command: $CMD"
echo "Data set: $DATA_SET"
echo "Number of processes: $NUM_PROCESSES"
echo "Profiling types enabled: ${TYPE_FLAGS[*]}"
echo "============================================================="

if [ ! -f ./build/$CMD ]; then
    echo "Error: No executable found for $CMD in ./build/"
    exit 1
fi

if [ ! -f ./build/$CMD-perf ]; then
    echo "Error: No executable found for $CMD-perf in ./build/"
    exit 1
fi

if [ ! -f ./build/$CMD-mpip ]; then
    echo "Error: No executable found for $CMD-mpip in ./build/"
    exit 1
fi

PERF_EVENTS=(
    cycles
    instructions
    branches
    branch-misses
    cache-references
    cache-misses
    stalled-cycles-frontend
    context-switches
)

EVENTS_STRING=$(IFS=,; echo "${PERF_EVENTS[*]}")

mkdir -p .outs/$CMD
mkdir -p .outs/$CMD/$DATA_SET
mkdir -p .outs/$CMD/$DATA_SET/mpi-$NUM_PROCESSES


# start perf with the associated file descriptors
echo 0  | sudo tee /proc/sys/kernel/nmi_watchdog > /dev/null
echo -1 | sudo tee /proc/sys/kernel/perf_event_paranoid > /dev/null

# check if stat enabled
if [ "$ENABLE_STAT" = "true" ]; then

    echo "=============================================================="
    echo "STARTING PERF STAT"
    echo "=============================================================="

    rm -rf .outs/$CMD/$DATA_SET/mpi-$NUM_PROCESSES/*.stat
    mpirun --oversubscribe  -np $NUM_PROCESSES bash -c '
        RANK=${OMPI_COMM_WORLD_RANK:-${PMI_RANK:-0}}
        PID=$$
        OUTDIR="'$PWD'/.outs/'$CMD'/'$DATA_SET'/mpi-'$NUM_PROCESSES'"

        TMP_DIR=/tmp
        FIFO_PREFIX=${TMP_DIR}/pfifo

        command rm -rf ${FIFO_PREFIX}.ack.${RANK}
        command rm -rf ${FIFO_PREFIX}.ctl.${RANK}
        command mkfifo ${FIFO_PREFIX}.ctl.${RANK}
        command mkfifo ${FIFO_PREFIX}.ack.${RANK}

        exec {perf_ctl_fd}<>${FIFO_PREFIX}.ctl.${RANK}
        exec {perf_ack_fd}<>${FIFO_PREFIX}.ack.${RANK}
        export PERF_CTL_FD=${perf_ctl_fd}
        export PERF_ACK_FD=${perf_ack_fd}

        PHYSICAL_CORES=8  # Adjust this to your actual physical core count

        if [ $RANK -lt $PHYSICAL_CORES ]; then
            # Use physical cores first: rank 0->core 0, rank 1->core 1, etc.
            CPU_ID=$RANK
        else
            # Use hyperthreads in round-robin: rank 8->core 8, rank 9->core 9, etc.
            CPU_ID=$((PHYSICAL_CORES + (RANK % PHYSICAL_CORES)))
        fi

        # Set CPU affinity
        taskset -cp $CPU_ID $PID

        exec perf stat \
            --delay 0 \
            -x , \
            --event=task-clock,cycles,instructions,L1-dcache-loads,L1-dcache-load-misses,L1-icache-loads,L1-icache-load-misses,branches,branch-misses \
            --output "$OUTDIR/rank_${RANK}.stat" \
            --control fd:${perf_ctl_fd},${perf_ack_fd} \
            -- '$PWD'/build/'$CMD' '$PWD'/.data/'$DATA_SET'/data.bin $OUTDIR/
        '

    # create a concatenated stat file
    STAT_FILE=".outs/$CMD/$DATA_SET/mpi-$NUM_PROCESSES/stat.csv"
    rm -f $STAT_FILE

    echo "Rank,Count,Unit,Event,RunTime,PrcRunTime,Metric,MetricUnit" > $STAT_FILE

    for file in .outs/$CMD/$DATA_SET/mpi-$NUM_PROCESSES/*.stat; do
        if [ -f "$file" ]; then
            echo "Processing $file"
            # echo "#Rank: $(basename "$file" .stat)"
            RANK=$(basename "$file" .stat)
            sed '/^#/d;/^$/d' "$file" | awk -v rank="$RANK" '{printf "%s,%s\n", rank, $0}' >> $STAT_FILE
        fi
    done

    # check if rbql is installed
    if command -v rbql &> /dev/null; then
        echo "Running RBQL to process the stat file..."

        rbql \
            --with-header \
            --delim , \
            --output .outs/$CMD/$DATA_SET/mpi-$NUM_PROCESSES/stat_processed.csv \
            --input .outs/$CMD/$DATA_SET/mpi-$NUM_PROCESSES/stat.csv \
            --query "select a.Event, avg(a.Metric) as avg, min(a.Metric) as min, max(a.Metric) as max, a.MetricUnit where a.Rank != 'rank_0' group by a.Event, a.MetricUnit" \
 

    else
        echo "RBQL is not installed. Skipping RBQL processing."
        echo "You can install RBQL using: pip install rbql"
    fi

fi


if [ "$ENABLE_RECORD" = "true" ]; then
    echo "=============================================================="
    echo "STARTING PERF RECORD"
    echo "=============================================================="

    rm -rf .outs/$CMD/$DATA_SET/mpi-$NUM_PROCESSES/*.perf
    mpirun --oversubscribe  -np $NUM_PROCESSES bash -c '
        RANK=${OMPI_COMM_WORLD_RANK:-${PMI_RANK:-0}}
        PID=$$
        OUTDIR="'$PWD'/.outs/'$CMD'/'$DATA_SET'/mpi-'$NUM_PROCESSES'"

        TMP_DIR=/tmp
        FIFO_PREFIX=${TMP_DIR}/pfifo

        command rm -rf ${FIFO_PREFIX}.ack.${RANK}
        command rm -rf ${FIFO_PREFIX}.ctl.${RANK}
        command mkfifo ${FIFO_PREFIX}.ctl.${RANK}
        command mkfifo ${FIFO_PREFIX}.ack.${RANK}

        exec {perf_ctl_fd}<>${FIFO_PREFIX}.ctl.${RANK}
        exec {perf_ack_fd}<>${FIFO_PREFIX}.ack.${RANK}
        export PERF_CTL_FD=${perf_ctl_fd}
        export PERF_ACK_FD=${perf_ack_fd}

        PHYSICAL_CORES=8  # Adjust this to your actual physical core count

        if [ $RANK -lt $PHYSICAL_CORES ]; then
            # Use physical cores first: rank 0->core 0, rank 1->core 1, etc.
            CPU_ID=$RANK
        else
            # Use hyperthreads in round-robin: rank 8->core 8, rank 9->core 9, etc.
            CPU_ID=$((PHYSICAL_CORES + (RANK % PHYSICAL_CORES)))
        fi

        # Set CPU affinity
        taskset -cp $CPU_ID $PID

        exec perf record \
            -g \
            --event="'$EVENTS_STRING'" \
            --output "$OUTDIR/rank_${RANK}.perf" \
            --control fd:${perf_ctl_fd},${perf_ack_fd} \
            -- '$PWD'/build/'$CMD'-perf '$PWD'/.data/'$DATA_SET'/data.bin $OUTDIR/
        '

fi

if [ "$ENABLE_MPIP" = "true" ]; then
    echo "=============================================================="
    echo "STARTING MPIP"
    echo "=============================================================="

    
    rm -rf .outs/$CMD/$DATA_SET/mpi-$NUM_PROCESSES/*.mpiP

    mpirun --oversubscribe -np $NUM_PROCESSES bash -c '
        RANK=${OMPI_COMM_WORLD_RANK:-${PMI_RANK:-0}}
        PID=$$
        OUTDIR="'$PWD'/.outs/'$CMD'/'$DATA_SET'/mpi-'$NUM_PROCESSES'"

        PHYSICAL_CORES=8  # Adjust this to your actual physical core count

        if [ $RANK -lt $PHYSICAL_CORES ]; then
            # Use physical cores first: rank 0->core 0, rank 1->core 1, etc.
            CPU_ID=$RANK
        else
            # Use hyperthreads in round-robin: rank 8->core 8, rank 9->core 9, etc.
            CPU_ID=$((PHYSICAL_CORES + (RANK % PHYSICAL_CORES)))
        fi

        taskset -cp $CPU_ID $PID
 
        export MPIP="-k 0 -f $OUTDIR"

        exec '$PWD'/build/'$CMD'-mpip '$PWD'/.data/'$DATA_SET'/data.bin $OUTDIR/
        '

    mv .outs/$CMD/$DATA_SET/mpi-$NUM_PROCESSES/*.mpiP .outs/$CMD/$DATA_SET/mpi-$NUM_PROCESSES/mpip.mpiP
fi


echo 2 | sudo tee /proc/sys/kernel/perf_event_paranoid > /dev/null
echo 1 | sudo tee /proc/sys/kernel/nmi_watchdog > /dev/null