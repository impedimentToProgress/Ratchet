The base ARMv6-m simulator has moved to: https://github.com/impedimentToProgress/thumbulator
The benchmarks have moved to: https://github.com/impedimentToProgress/MiBench2

This folder contains our important scripts for testing & benchmarking as well as
the source for our benchmarks.

Makefile.mk
  Generalized Makefile for all of our benchmarks. Each benchmark uses
  Makefile.mk to compile. Note that it relies on two environment variables
  OPTLVL and NOIDEMCOMP which set the desired optimization level and a flag to
  force compilation without Ratchet's instrumentation.

  That is, a program compiled with OPTLVL=-03 and NOIDEMCOMP=1 would be compiled
  at -O3 without Ratchet, while OPTLVL=-O0 and NOIDEMCOMP=0 would be compiled at
  -O0 with Ratchet.

regression.py
  Automates correctness tests on benchmarks. This test ensures that each
  benchmark can handle simulated failures using the GDB front-end of the
  simulator.

  This can be invoked by 
    python regression.py
  In order to change the number of iterations, failure frequency and standard
  deviation, please change the variables in the script.

  REQUIRES:
    python/ directory to be symlinked to the GDB python scripts so that they are
    loaded when GDB starts up.
      
  TODO: make these accept command line parameters.

correctness.sh
  Checks to see that every WAR dependency is separated by a checkpoint. This
  requires all benchmarks to be compiled with Ratchet and the simulator compiled
  with the following macros set:
    REPORT_IDEM_BREAKS=1
    PRINT_CHECKPOINTS=1
  No arguments.

stats.sh
  This script spawns processes for both instrumented and uninstrumented versions
  of the benchmarks and reports the overall overhead of instrumented versions.
  In addition, it generates benchmark.results which has per-test cycle counts.
  No arguments.

make_all.sh
  Recompiles all benchmarks. No arguments.

checkpoint.c
  C-file that contains definitions of each checkpoint. It includes inline
  assembly for each differently sized checkpoints.

clib
	location of instrumented libraries
	run.sh can do downloading and patching
	requires Ratchet LLVM
barebench
	check_idem.py
		deprecated: run on memory access log from simulator to sanity check instrumentation
		replaced in-simulator version
	checkpoint.c
		all different sized checkpoints
		brought into binary at link time to match symbol in binary left by LLVM
		performance bonus by incorporating this into LLVM
	clear_results.sh
		clear sim logs
		probably deprecated
	correctness.sh
		runs the simulator, gets stdio pipe, checks for idem violations reported by in-simulator idem violations
	count_cp.sh
		input: program to simulator
		runs sim, get stdio and stderr pipes, and counts for each checkpoints
		checkpoint here is the common backup N regs code
	gdbtools.py
		script that is loaded by gdb
		imports the scripts in python
flow for re-execution testing
	make the binaries for the programs I want to test
	regression.py
		edit variables in test()
		edit iters at bottom of file
	python regression.py test | verify
		test just runs the programs
		verify checks hash of memory for correctness
	get_addr.sh
		get address of each checkpoint in assembly file
		after each checkpoint modification
			run this on a binary with checkpoints
			amend sim main
	idemvectors.s
		start: ratchet start-up code
		check_cp: checks to see if a cp has occurred during this execution
		cp_except: way to call a cp from watchdog timer exception
	liveregs
		measure how long a checkpoint takes
		makes a simple checkpoint program
	makeall.sh
		makes all benchmarks in parallel!!!
	makefile.mk
		export NOIDEMCOMP=1
			compile without idem checks
	mem_count.sh
		output number of loads and stores for baseline and w/CP
	overhead.sh
		output number of cycles due to adding CP
		output binary size diff
	stats.sh
		overhead, but in parallel

