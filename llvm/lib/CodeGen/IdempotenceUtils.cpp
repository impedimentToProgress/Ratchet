//===-------- IdempotenceUtils.cpp ------------------------------*- C++ -*-===//
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

#include "IdempotenceUtils.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/SlotIndexes.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineIdempotentRegions.h"
#include "llvm/Support/Debug.h"
using namespace llvm;

// Scan forward from MI populating Slots with all the slot ranges encountered
// before hitting Region's exits.
void llvm::mapRegionSlots(const IdempotentRegion &Region,
                          const SlotIndexes &Indexes,
                          SlotInterval *Slots) {
  mapSuccessorSlotsOfMIInRegion(Region.getEntry(), Region, Indexes, Slots);
}

void llvm::mapSuccessorSlotsOfMIInRegion(const MachineInstr &MI,
                                         const IdempotentRegion &Region,
                                         const SlotIndexes &Indexes,
                                         SlotInterval *Slots) {
  Slots->clear();
  IdempotentRegion::const_mbb_iterator RI(Region, MI.getParent(), &MI);
  for (; RI.isValid(); ++RI) {
    SlotIndex Start, End;
    std::tie(Start, End) = RI.getSlotRange(Indexes);
    Slots->insert(Start, End, true);
  }
}

bool llvm::isCalleeSavedRegister(unsigned Reg, const TargetRegisterInfo &TRI) {
  const MCPhysReg *CSRegs = TRI.getCalleeSavedRegs();
  for (unsigned I = 0; CSRegs[I]; ++I)
    if (Reg == CSRegs[I])
      return true;
  return false;
}

