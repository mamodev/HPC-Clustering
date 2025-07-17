#!/bin/bash
# Configure Extrae
export EXTRAE_CONFIG_FILE=./extrae.xml
# Load the tracing library (choose C/Fortran)
export LD_PRELOAD=/usr/lib/libmpitrace.so
#export LD_PRELOAD=${EXTRAE_HOME}/lib/libmpitracef.so
# Run the program
$*