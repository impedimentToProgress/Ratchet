Low Level Virtual Machine (LLVM)
================================

This directory and its subdirectories contain source code for LLVM,
a toolkit for the construction of highly optimized compilers,
optimizers, and runtime environments.

LLVM is open source software. You may freely distribute it under the terms of
the license agreement found in LICENSE.txt.

Please see the documentation provided in docs/ for further
assistance with LLVM, and in particular docs/GettingStarted.rst for getting
started with LLVM and docs/README.txt for an overview of LLVM's
documentation setup.

If you're writing a package for LLVM, see docs/Packaging.rst for our
suggestions.



RATCHET
=======

The Ratchet front-end is implemented as the MemoryIdempotenceAnalysis and
ConstructIdempotentRegions passes. Their .cpp files are located in lib/CodeGen.
MemoryIdempotenceAnalysis performs the identification of all front-end WAR
dependencies and identifies the optimal locations for checkpoints.
ConstructIdempotentRegions lowers the checkpoint to an intrinsic that is
recognized by the back-end.

The Ratchet back-end is implemented in
lib/CodeGen/MachineIdempotentRegions.cpp. This uses the Target abstraction to
lower checkpoints, catch spilled register WAR dependencies, and remove duplicate
instructions. The target specific functions called in this file are located in
lib/Target/ARM/ARMBaseInstrInfo.cpp. 

We also add some code to lib/Target/ARM/Thumb1FrameLowering.cpp to use our
custom return checkpoint and replace any pop instructions with a non-destructive
read.
