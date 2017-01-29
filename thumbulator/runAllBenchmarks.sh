#!/bin/sh

# Build all benchmarks
#bash -c "cd tests && sh buildAllBenchmarks.sh"

# Run all benchmarks
rm tests/*.bin.out tests/*.bin.err
find tests/ -d 1 -name "*.bin" -exec bash -c "./sim_main {} 2> /dev/null > {}.out" \;
