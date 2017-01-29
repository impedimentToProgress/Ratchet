The .c files in this directory make up the simulator used by Ratchet. Most of
the macros that control the simulator settings can be found in sim_support.h.

The GDB server code is located in rsp-server.*. In order to connect to the
simulator using GDB you can run with the -g flag:
    ./sim_main -g <filename>.bin
and then run GDB and run the following commands:
    file <filename>.elf
    target remote :272727
This should connect the GDB instance to the simulator before the simulator has
started executing the program. From there, simple gdb commands can be used to
start debugging.

The bareBench/ folder contains important scripts for use with GDB to simulate
powerfailures as well as our MIBench benchmarks.
