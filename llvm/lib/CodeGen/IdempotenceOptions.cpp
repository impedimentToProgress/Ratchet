//===-------- IdempotenceOptions.cpp ----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains idempotence-specific compilation options.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/IdempotenceOptions.h"
#include "llvm/Support/CommandLine.h"
using namespace llvm;

namespace llvm {

cl::opt<bool> IdempotenceVerify(
    "idempotence-verify", cl::Hidden,
    cl::desc("Verify region construction and idempotence preservation"),
    cl::init(false));

cl::opt<IdempotenceOptions::ConstructionMode> IdempotenceConstructionMode(
    "idempotence-construction", cl::Hidden,
    cl::desc("Idempotent region construction mode"),
    cl::values(clEnumValN(IdempotenceOptions::NoConstruction, 
                          "none", "No region construction"),
               clEnumValN(IdempotenceOptions::OptimizeForSize, 
                          "size", "Construct optimized for size"),
               clEnumValN(IdempotenceOptions::OptimizeForSpeed,
                          "speed", "Construct optimized for speed"),
               clEnumValN(IdempotenceOptions::OptimizeForIdeal,
                          "ideal", "Construct optimized for ideal case"),
               clEnumValN(IdempotenceOptions::BranchRecovery,
                          "branch", "Construct for branch recovery"),
               clEnumValEnd),
    cl::init(IdempotenceOptions::NoConstruction));

cl::opt<IdempotenceOptions::PreservationMode> IdempotencePreservationMode(
    "idempotence-preservation", cl::Hidden,
    cl::desc("Idempotence preservation mode"),
    cl::values(clEnumValN(IdempotenceOptions::NoPreservation,
                          "none", "Do not preserve idempotence"),
               clEnumValN(IdempotenceOptions::VariableCF,
                          "vcf", "Preserve assuming variable control flow"),
               clEnumValN(IdempotenceOptions::InvariableCF,
                          "icf", "Preserve assuming invariable control flow"),
               clEnumValEnd),
    cl::init(IdempotenceOptions::NoPreservation));

} // namespace llvm

