#!/bin/bash

if [ -z "$1" ] || [ -z "$2" ] || [ -z "$3" ]; then
    cat << EOF
To few arguments

Usage: ./run_test.sh RUNS SIMULATION-NAME SIMULATION-FILE

RUNS define how many times to run the same simulation
SIMULATION-NAME is simply the name of the output files
SIMULATION-FILE is the simulation that will be run, probably a .csc file

Example usage:
./$0 5 first-simulation ./evaluation/idle-tree-sparse-lossy.csc
EOF
    exit
fi

CURRENT=$(pwd)
COOJA=../../../../tools/cooja/

BORDER_ROUTER=$(find . -name border-router.native)

if [ -z "$BORDER_ROUTER" ]; then
    echo "Couldnt find border-router.native"
    exit
fi

DIR="."
while true; do
    COOJA=$(find $DIR -name cooja.jar)

    if [ -n "$COOJA" ]; then
        break
    fi
    DIR="$DIR/.."
done

COOJA=$(dirname "$COOJA")

SIMULATION=$CURRENT/$3

simname=$2

RUNS=$1

echo "Found cooja.jar in $COOJA"

cd "$COOJA" || { echo >&2 "Failed to cd into $COOJA"; exit 1 }
for i in $(seq 1 "$RUNS"); do
    java -mx512m -jar ../dist/cooja.jar -nogui="$SIMULATION" > /dev/null & 
    sleep 5
    sudo "$CURRENT/$BORDER_ROUTER" -a 127.0.0.1 aaaa::1/64
    wait
    mv COOJA.log "$CURRENT/$simname-$i.log"
    mv COOJA.testlog "$CURRENT/$simname-$i.testlog"
done
