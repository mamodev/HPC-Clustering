
#!/bin/bash
TMP_DIR=/tmp
FIFO_PREFIX=${TMP_DIR}/pfifo
mkdir -p .outs/$1
mkdir -p .outs/$1/$2

command rm -rf ${FIFO_PREFIX}.ack
command rm -rf ${FIFO_PREFIX}.ctl

command mkfifo ${FIFO_PREFIX}.ctl
command mkfifo ${FIFO_PREFIX}.ack
 
# associate file descriptors
exec {perf_ctl_fd}<>${FIFO_PREFIX}.ctl
exec {perf_ack_fd}<>${FIFO_PREFIX}.ack
 
# set env vars for application
export PERF_CTL_FD=${perf_ctl_fd}
export PERF_ACK_FD=${perf_ack_fd}
 
# start perf with the associated file descriptors
echo 0  | sudo tee /proc/sys/kernel/nmi_watchdog > /dev/null
echo -1 | sudo tee /proc/sys/kernel/perf_event_paranoid > /dev/null


# # echo STARTING PERF STAT
# if [ -f .outs/$1/$2/stat.txt ]; then
#     mv .outs/$1/$2/stat.txt .outs/$1/$2/stat.txt.old
# fi
# perf stat \
#     --delay=-1 \
#     --event=task-clock,cycles,instructions,L1-dcache-loads,L1-dcache-load-misses,L1-icache-loads,L1-icache-load-misses,branches,branch-misses \
#     --control fd:${perf_ctl_fd},${perf_ack_fd} \
#     --output .outs/$1/$2/stat.txt \
#     -- ./build/$1-perf .data/$2/data.bin .outs/$1/$2/

#     # -- taskset -c 5 ./build/$1 .data/$2/data.bin .outs/$1/$2 \

echo "STARTING PERF RECORD"
perf record \
    -g \
    --delay=-1 \
    --event=task-clock,cycles,instructions,L1-dcache-loads,L1-dcache-load-misses,L1-icache-loads,L1-icache-load-misses,branches,branch-misses \
    --control fd:${perf_ctl_fd},${perf_ack_fd} \
    --output .outs/$1/$2/perf \
    -- ./build/$1 .data/$2/data.bin .outs/$1/$2/

    # -- taskset -c 5 ./build/$1-perf .data/$2/data.bin .outs/$1/$2/

# exec {perf_ctl_fd}>&-
# exec {perf_ack_fd}>&-

echo 2 | sudo tee /proc/sys/kernel/perf_event_paranoid > /dev/null
echo 1 | sudo tee /proc/sys/kernel/nmi_watchdog > /dev/null