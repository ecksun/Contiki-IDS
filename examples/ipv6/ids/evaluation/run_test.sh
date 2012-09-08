#!/bin/bash

# Usage: ./run_test.sh 5 simname simulation.csc

CURRENT=$(pwd)
COOJA=../../../../tools/cooja/

SIMULATION=$CURRENT/$3

simname=$2

RUNS=$1

cd $COOJA/dist
for i in $(seq 1 $RUNS); do
    java -mx512m -jar ../dist/cooja.jar -nogui=$SIMULATION
    mv COOJA.log $CURRENT/$simname-$i.log
    mv COOJA.testlog $CURRENT/$simname-$i.testlog
done
