#!/bin/bash
# MODIFIED PROFILING SCRIPT FOR MPIP
# Usage: ./perf-mpi.sh <cmd> <data_set> <num_processes> [...flags]
# where flags are:
#   --type=<type> (default: stat, options: stat, record, mpip, self, all)
#   --keep-deltas (optional, keeps previous delta files)
#   --agg-deltas (optional, aggregates delta files)
#   short flags: -kd (keep deltas), -t (type), -ad (aggregate deltas)

PHYSICAL_CORES=8

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

KEEP_DELTAS="false"
if [[ " ${FLAGS[*]} " == *" --keep-deltas "* ]] || [[ " ${FLAGS[*]} " == *" -kd "* ]]; then
    KEEP_DELTAS="true"
fi

AGGREGATE_DELTAS="false"
if [[ " ${FLAGS[*]} " == *" --agg-deltas "* ]] || [[ " ${FLAGS[*]} " == *" -ad "* ]]; then
    AGGREGATE_DELTAS="true"
fi


TYPE_FLAGS=()
#filter all flags that start with --type=
for flag in "${FLAGS[@]}"; do
    if [[ $flag == --type=* ]]; then
        TYPE=${flag#--type=}
        TYPE_FLAGS+=("$TYPE")
    fi
    if [[ $flag == -t=* ]]; then
        TYPE=${flag#-t=}
        TYPE_FLAGS+=("$TYPE")
    fi
done

if [ ${#TYPE_FLAGS[@]} -eq 0 ]; then
    TYPE_FLAGS=("stat")
fi

ENABLE_STAT="false"
ENABLE_RECORD="false"
ENABLE_MPIP="false"
ENABLE_SELF="false"


if [[ " ${TYPE_FLAGS[*]} " == *" all "* ]]; then
    ENABLE_STAT="true"
    ENABLE_RECORD="true"
    ENABLE_MPIP="true"
    ENABLE_SELF="true"
fi

if [[ " ${TYPE_FLAGS[*]} " == *" self "* ]]; then
    ENABLE_SELF="true"
fi

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

# check if keep deltas is enabled
if [ "$KEEP_DELTAS" = "false" ]; then
    if [ -d .outs/$CMD/$DATA_SET/mpi-$NUM_PROCESSES ]; then
        rm -rf .outs/$CMD/$DATA_SET/mpi-$NUM_PROCESSES/delta_*.csv
    fi
fi

agg_deltas() {
    INPUT_FILE=$1
    OUTPUT_FILE=$2

    if command -v rbql &> /dev/null; then
        echo "Running RBQL to process the delta_w file..."
        rbql \
            --with-header \
            --delim , \
            --output $OUTPUT_FILE \
            --input $INPUT_FILE \
            --query "select a.Event, 
                avg(a.Time) as avg,
                min(a.Time) as min, 
                max(a.Time) as max, 
                sum(a.Time) as sum,
                count(a.Event) as count
                group by a.Event"

        # use awk to truncate decimal places to 0 (no decimal places)
        # awk -F, 'BEGIN {OFS=","} {if (NR==1) print $0; else print $1, int($2), int($3), int($4), $5}' $OUTPUT_FILE > temp && mv temp $OUTPUT_FILE
        awk -F, 'BEGIN {OFS=","} {if (NR==1)
             print $0; else print $1, int($2), int($3), int($4), int($5), $6}' $OUTPUT_FILE > temp && mv temp $OUTPUT_FILE
    else
        echo "RBQL is not installed. Skipping RBQL processing. for $INPUT_FILE"
        echo " | {info} You can install RBQL using: pip install rbql"
    fi
}

process_deltas() {
    PROF=$1
    BASE_PATH=".outs/$CMD/$DATA_SET/mpi-$NUM_PROCESSES"
    DELTAS_FILES=$(ls $BASE_PATH/delta_w_*.csv 2>/dev/null | grep -v ".$PROF.csv")

    if [ -z "$DELTAS_FILES" ]; then
        echo "No delta_w_*.csv files found in $BASE_PATH/"
    else
        # Enssure format is event,time if not print error and continue
        # concat all files into one file delta_w.csv
        echo "Concatenating delta_w_*.csv files into delta_w.csv"
        AGG_FILE="$BASE_PATH/delta_agg_w.csv"

        if [ ! -f $AGG_FILE ]; then
            echo "Event,Time" > $AGG_FILE
        fi
        
        for file in $DELTAS_FILES; do
            cat "$file" | awk -F, '{print $1","$2}' >> $AGG_FILE
            FILE_NAME=$(basename "$file" .csv)
            mv "$file" "$BASE_PATH/$FILE_NAME.$PROF.csv"
        done

        agg_deltas "$AGG_FILE" "$BASE_PATH/delta_rbql_proc.csv"
    fi

    MASTER_FILE="$BASE_PATH/delta_master.csv"

    if [ -f "$MASTER_FILE" ]; then
        echo "Processing delta_master.csv"
        AGG_FILE="$BASE_PATH/delta_agg_master.csv"
        if [ ! -f $AGG_FILE ]; then
            echo "Event,Time" > $AGG_FILE
        fi

        cat "$MASTER_FILE" | awk -F, '{print $1","$2}' >> $AGG_FILE
        FILE_NAME=$(basename "$MASTER_FILE" .csv)
        mv "$MASTER_FILE" "$BASE_PATH/$FILE_NAME.$PROF.csv"

        agg_deltas "$AGG_FILE" "$BASE_PATH/delta_rbql_master.csv"
    else
        echo "No delta_master.csv file found in $BASE_PATH/"
    fi
}


if [ "$(cat /proc/sys/kernel/nmi_watchdog)" -ne 0 ]; then
    echo "Disabling nmi_watchdog..."
    echo 0 | sudo tee /proc/sys/kernel/nmi_watchdog > /dev/null
fi

if [ "$(cat /proc/sys/kernel/perf_event_paranoid)" -ne -1 ]; then
    echo "Setting perf_event_paranoid to -1..."
    echo -1 | sudo tee /proc/sys/kernel/perf_event_paranoid > /dev/null
fi

if [ "$ENABLE_SELF" = "true" ]; then
    echo "=============================================================="
    echo "STARTING SELF PROFILING"
    echo "=============================================================="

    rm -rf .outs/$CMD/$DATA_SET/mpi-$NUM_PROCESSES/*.self
    mpirun --oversubscribe -np $NUM_PROCESSES bash -c '
        RANK=${OMPI_COMM_WORLD_RANK:-${PMI_RANK:-0}}
        PID=$$
        OUTDIR="'$PWD'/.outs/'$CMD'/'$DATA_SET'/mpi-'$NUM_PROCESSES'"

        PHYSICAL_CORES='$PHYSICAL_CORES'
        if [ $RANK -lt $PHYSICAL_CORES ]; then
            # Use physical cores first: rank 0->core 0, rank 1->core 1, etc.
            CPU_ID=$RANK
        else
            # Use hyperthreads in round-robin: rank 8->core 8, rank 9->core 9, etc.
            CPU_ID=$((PHYSICAL_CORES + (RANK % PHYSICAL_CORES)))
        fi

        taskset -cp $CPU_ID $PID > /dev/null 2>&1

        exec '$PWD'/build/'$CMD'-asserts '$PWD'/.data/'$DATA_SET'/data.bin $OUTDIR/


        #   > /dev/null 2>&1
        '

    process_deltas "self"

fi

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

        PHYSICAL_CORES='$PHYSICAL_CORES'  # Adjust this to your actual physical core count

        if [ $RANK -lt $PHYSICAL_CORES ]; then
            # Use physical cores first: rank 0->core 0, rank 1->core 1, etc.
            CPU_ID=$RANK
        else
            # Use hyperthreads in round-robin: rank 8->core 8, rank 9->core 9, etc.
            CPU_ID=$((PHYSICAL_CORES + (RANK % PHYSICAL_CORES)))
        fi

        taskset -cp $CPU_ID $PID > /dev/null 2>&1

        exec perf stat \
            --delay -1 \
            -x , \
            --event=task-clock,cycles,instructions,L1-dcache-loads,L1-dcache-load-misses,L1-icache-loads,L1-icache-load-misses,branches,branch-misses \
            --output "$OUTDIR/rank_${RANK}.stat" \
            --control fd:${perf_ctl_fd},${perf_ack_fd} \
            -- '$PWD'/build/'$CMD' '$PWD'/.data/'$DATA_SET'/data.bin $OUTDIR/ > /dev/null 2>&1


            # -- '$PWD'/build/'$CMD' '$PWD'/.data/'$DATA_SET'/data.bin $OUTDIR/ 2>&1 | grep -v "Events" | sed "s/^/\tR($RANK): /"
        '

    create a concatenated stat file
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
    
    if [ "$AGGREGATE_DELTAS" = "true" ]; then
        process_deltas "stat"
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

        PHYSICAL_CORES='$PHYSICAL_CORES'  # Adjust this to your actual physical core count

        if [ $RANK -lt $PHYSICAL_CORES ]; then
            # Use physical cores first: rank 0->core 0, rank 1->core 1, etc.
            CPU_ID=$RANK
        else
            # Use hyperthreads in round-robin: rank 8->core 8, rank 9->core 9, etc.
            CPU_ID=$((PHYSICAL_CORES + (RANK % PHYSICAL_CORES)))
        fi

        taskset -cp $CPU_ID $PID > /dev/null 2>&1

        exec perf record \
            -g \
            --event="'$EVENTS_STRING'" \
            --output "$OUTDIR/rank_${RANK}.perf" \
            --control fd:${perf_ctl_fd},${perf_ack_fd} \
            -- '$PWD'/build/'$CMD'-perf '$PWD'/.data/'$DATA_SET'/data.bin $OUTDIR/  > /dev/null 2>&1
        '
    
    if [ "$AGGREGATE_DELTAS" = "true" ]; then
        process_deltas "record"
    fi
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

        PHYSICAL_CORES='$PHYSICAL_CORES'  # Adjust this to your actual physical core count

        if [ $RANK -lt $PHYSICAL_CORES ]; then
            # Use physical cores first: rank 0->core 0, rank 1->core 1, etc.
            CPU_ID=$RANK
        else
            # Use hyperthreads in round-robin: rank 8->core 8, rank 9->core 9, etc.
            CPU_ID=$((PHYSICAL_CORES + (RANK % PHYSICAL_CORES)))
        fi

        taskset -cp $CPU_ID $PID > /dev/null 2>&1
 
        export MPIP="-k 0 -f $OUTDIR"

        exec '$PWD'/build/'$CMD'-mpip '$PWD'/.data/'$DATA_SET'/data.bin $OUTDIR/ > /dev/null 2>&1
        '

    mv .outs/$CMD/$DATA_SET/mpi-$NUM_PROCESSES/*.mpiP .outs/$CMD/$DATA_SET/mpi-$NUM_PROCESSES/mpip.mpiP

    if [ "$AGGREGATE_DELTAS" = "true" ]; then
        process_deltas "mpip"
    fi
fi


if [ "$(cat /proc/sys/kernel/nmi_watchdog)" -ne 1 ]; then
    echo "Enabling nmi_watchdog..."
    echo 1 | sudo tee /proc/sys/kernel/nmi_watchdog > /dev/null
fi

if [ "$(cat /proc/sys/kernel/perf_event_paranoid)" -ne 2 ]; then
    echo "Setting perf_event_paranoid to 2..."
    echo 2 | sudo tee /proc/sys/kernel/perf_event_paranoid > /dev/null
fi

# check if there are any delta files
if ls .outs/$CMD/$DATA_SET/mpi-$NUM_PROCESSES/delta_*.csv 1> /dev/null 2>&1; then
    echo "=============================================================="
    echo "Delta files found in .outs/$CMD/$DATA_SET/mpi-$NUM_PROCESSES/"
    echo "=============================================================="
    
    echo "Master delta agg:"
    cat .outs/$CMD/$DATA_SET/mpi-$NUM_PROCESSES/delta_rbql_master.csv | column -s, -t
    echo ""
    echo "Worker deltas agg:"
    cat .outs/$CMD/$DATA_SET/mpi-$NUM_PROCESSES/delta_rbql_proc.csv | column -s, -t
fi