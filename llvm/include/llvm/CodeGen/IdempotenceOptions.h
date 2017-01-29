//===-------- IdempotenceOptions.h ------------------------------*- C++ -*-===//
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

#ifndef LLVM_CODEGEN_IDEMPOTENCEOPTIONS_H
#define LLVM_CODEGEN_IDEMPOTENCEOPTIONS_H

#include "llvm/Support/CommandLine.h"
using namespace llvm;

namespace llvm {

  namespace IdempotenceOptions {
    enum ConstructionMode {
      /// NoConstruction (Default) - Don't construct idempotent regions.
      NoConstruction,

      /// OptimizeForSize - Construct idempotent regions for maximum size.
      OptimizeForSize,

      /// OptimizeForSpeed - Construction idempotent regions for maximum
      /// speed of generated code.
      OptimizeForSpeed,

      OptimizeForIdeal,

      /// BranchRecovery - Construction idempotent regions to minimize
      /// re-execution costs due to branch mis-predictions.
      BranchRecovery
    };

    enum PreservationMode {
      /// NoPreservation (Default) - Do not preserve the idempotence property
      /// through resource allocation, even if regions are demarcated.
      NoPreservation,

      /// VariableCF - Preserve the idempotence property assuming potentially
      /// variable control flow upon re-execution ("contextual idempotence" in
      /// dissertation).
      VariableCF,

      /// InvariableCF - Preserve the idempotence property assuming invariable
      /// control flow upon re-execution ("architectural idempotence" in 
      /// dissertation).
      InvariableCF
    };
  }

  // Command line flag storage declared here.
  extern cl::opt<bool> IdempotenceVerify;
  extern cl::opt<IdempotenceOptions::ConstructionMode>
    IdempotenceConstructionMode;
  extern cl::opt<IdempotenceOptions::PreservationMode>
    IdempotencePreservationMode;

} // namespace llvm

#endif // LLVM_CODEGEN_IDEMPOTENCEOPTIONS_H

