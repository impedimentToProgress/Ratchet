#!/bin/bash

TEST='hello'
echo $TEST

make clean; NOIDEMCOMP=0 make all
IDEMTIME=`../../sim_main main.bin 2>>/dev/null | grep "Program exit after [0-9]*" | awk '{print $4}'`
IDEMSIZE=`stat -x main.elf | grep "Size: [0-9]*" | awk '{print $2}'`
make clean; NOIDEMCOMP=1 make all
CLEANTIME=`../../sim_main main.bin 2>>/dev/null | grep "Program exit after [0-9]*" | awk '{print $4}'` 
CLEANSIZE=`stat -x main.elf | grep "Size: [0-9]*" | awk '{print $2}'`

TIMEOVER=$(bc -l <<< $IDEMTIME/$CLEANTIME)
SPACEOVER=$(bc -l <<< $IDEMSIZE/$CLEANSIZE)
echo "IDEM cycles: $IDEMTIME cycles"
echo "REG  cycles: $CLEANTIME cycles"
echo "Time overhead: $TIMEOVER x"
echo "Size overhead: $SPACEOVER x"
