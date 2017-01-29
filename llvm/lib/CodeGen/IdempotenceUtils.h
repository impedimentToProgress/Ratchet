//===-------- IdempotenceUtils.h --------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains idempotence-specific helpers.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_IDEMPOTENCEUTILS_H
#define LLVM_CODEGEN_IDEMPOTENCEUTILS_H

#include "llvm/ADT/IntervalMap.h"
#include "llvm/CodeGen/LiveIntervalAnalysis.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineIdempotentRegions.h"

namespace llvm {
typedef IntervalMap<SlotIndex, bool> SlotInterval;

// mapRegionSlots - Return in Slots the slot ranges in Region.
void mapRegionSlots(const IdempotentRegion &Region,
                    const SlotIndexes &Indexes,
                    SlotInterval *Slots);

// mapSuccessorSlotsOfMIInRegion - Return in Slots the slot ranges in region
// that are reachable from MI.
void mapSuccessorSlotsOfMIInRegion(const MachineInstr &MI,
                                   const IdempotentRegion &Region,
                                   const SlotIndexes &Indexes,
                                   SlotInterval *Slots);

// isCalleeSavedRegister - Return whether Reg is a callee-saved register.
bool isCalleeSavedRegister(unsigned Reg, const TargetRegisterInfo &TRI);
}

#endif // LLVM_CODEGEN_IDEMPOTENCEUTILS_H

