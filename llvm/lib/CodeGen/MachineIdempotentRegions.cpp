//===-------- MachineIdempotentRegions.cpp ----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the implementation for querying and updating the
// idempotent region information at the machine level.  A "machine" idempotent
// region is defined by the single IDEM instruction that defines its entry point
// and it spans all instructions reachable by control flow from the entry point
// to subsequent IDEM instructions.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "machine-idempotent-regions"
#include "llvm/CodeGen/MachineIdempotentRegions.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/IdempotenceOptions.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Target/TargetInstrInfo.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Target/TargetRegisterInfo.h"

using namespace llvm;

//===----------------------------------------------------------------------===//
// IdempotentRegion
//===----------------------------------------------------------------------===//

void IdempotentRegion::dump() const {
  print(dbgs());
}

void IdempotentRegion::print(raw_ostream &OS, const SlotIndexes *SI) const {
  OS << "IR#" << ID_ << " ";
  if (SI)
    OS << "@" << SI->getInstructionIndex(&getEntry()) << " ";
  OS << "in BB#" << getEntryMBB().getNumber();
}

raw_ostream &llvm::operator<<(raw_ostream &OS, const IdempotentRegion &R) {
  R.print(OS);
  return OS;
}

//===----------------------------------------------------------------------===//
// MachineIdempotentRegions
//===----------------------------------------------------------------------===//

char MachineIdempotentRegions::ID = 0;
INITIALIZE_PASS_BEGIN(MachineIdempotentRegions,
                "machine-idempotence-regions",
                "Machine Idempotent Regions", false, true)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTree)
INITIALIZE_PASS_END(MachineIdempotentRegions,
                "machine-idempotence-regions",
                "Machine Idempotent Regions", false, true)

FunctionPass *llvm::createMachineIdempotentRegionsPass() {
  return new MachineIdempotentRegions();
}

void MachineIdempotentRegions::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<MachineDominatorTree>();
  AU.setPreservesAll();
  MachineFunctionPass::getAnalysisUsage(AU);
}

void MachineIdempotentRegions::releaseMemory() {
  RegionAllocator_.Reset();
  Regions_.clear();
  EntryToRegionMap_.clear();
}

bool MachineIdempotentRegions::runOnMachineFunction(MachineFunction &MF) {
  assert(IdempotenceConstructionMode != IdempotenceOptions::NoConstruction &&
         "pass should not be run");

  std::string triple = MF.getTarget().getTargetTriple();
  if(triple.find("arm") != std::string::npos || triple.find("thumb") != std::string::npos)
  {

  MF_  = &MF;
  TII_ = MF.getSubtarget().getInstrInfo();
  TRI_ = MF.getSubtarget().getRegisterInfo();
  DT_  = &getAnalysis<MachineDominatorTree>();

  DEBUG(dbgs() << "*** Machine Idempotent Regions Pass *** Function:" << MF.getName() <<"\n");

  //// Fix Pops
  //TII_->replaceWithIdemPop(MF);

  // Get rid of dummy calls
  killDummyCalls(MF);

  //// Take care of idempotency breaks between calls
  //wrapCalls(MF);

  // Fix violations caused by stack spills
  fixStackSpills(MF);

  //TII_->fixIdemCondCodes(MF);

  // Remove duplicate checkpoints
  removeDuplicates(MF);

  // Lower the region entries to checkpoints.
  lowerIdemToCheckpoint(MF); 

  DEBUG(dbgs() << "*** End MIR Pass *** Function:" << MF.getName() <<"\n");
  }

  return false;
}

// We ran into a problem where we would insert a checkpoint into a function that
// did not expect to have any calls in it. As such it would find no need to save
// it's link register. This is a very hacky fix to that, we always insert a
// dummy call into functions we expect to checkpoint.
//
// This function just scans to find all these dummy calls and removes them.
// There must be a better way...
void MachineIdempotentRegions::killDummyCalls(MachineFunction &MF)
{
  for (MachineFunction::iterator B = MF.begin(), BE = MF.end(); B != BE; ++B)
    for (MachineBasicBlock::iterator I = B->begin(); I != B->end(); ++I)
      if(I->isCall())
        for (MachineInstr::mop_iterator MOP = I->operands_begin(); MOP != I->operands_end(); MOP++)
          if (MOP->isGlobal())
          {
            std::string func(MOP->getGlobal()->getName());
            //DEBUG(dbgs() << "JVDW: found " << func << "\n");
            if (func.compare("__dummyfunc") == 0)
            {
              I->eraseFromParent();
              I = B->begin();
              break;
            }
          }
}

// In order to compensate for our intra-procedural alias analysis we need to
// checkpoint before and after calls. One way to do this is introduce
// checkpoints before a call to a function or at the beginning of every function
// NOTE: This function should no longer be used, we took care of it in the front
// end.
void MachineIdempotentRegions::wrapCalls(MachineFunction &MF)
{
  //TII_->emitIdemBoundary(*MF.begin(), MF.begin()->begin());


  // Regions start at idem boundaries.
  for (MachineFunction::iterator B = MF.begin(), BE = MF.end(); B != BE; ++B)
    for (MachineBasicBlock::iterator I = B->begin(); I != B->end(); ++I)
      if (TII_->isIdemBoundary(I))
        createRegionAtBoundary(I);
      else if (I->isCall())
      {
        createRegionBefore(I->getParent(), I);
        //createRegionBefore(I->getParent(), I);
      }
}

// One of the consequences of spilling registers to the stack is it results in
// an idempotency violation. Insert checkpoints.
void MachineIdempotentRegions::fixStackSpills(MachineFunction &MF)
{
  if(IdempotenceConstructionMode == IdempotenceOptions::OptimizeForIdeal)
    return;

  int FI;
  for (MachineFunction::iterator B = MF.begin(), BE = MF.end(); B != BE; ++B)
    for (MachineBasicBlock::iterator I = B->begin(); I != B->end(); ++I)
      if(I->mayStore())
      {
        findAntidependencePairs(I);
      }
}

bool MachineIdempotentRegions::searchForPriorBoundaries(MachineBasicBlock::iterator I)
{
  // Perform a reverse depth-first search to find aliasing loads.
  typedef std::pair<MachineBasicBlock *, MachineBasicBlock::iterator> WorkItem;
  SmallVector<WorkItem, 8> Worklist;
  SmallPtrSet<MachineBasicBlock *, 32> Visited;

  MachineBasicBlock *StartBB = I->getParent();
  if (I == StartBB->begin())
    return false;
  Worklist.push_back(WorkItem(StartBB, std::prev(I)));
  MachineBasicBlock::iterator DominatingIdem;
  do {
    MachineBasicBlock *BB;
    MachineBasicBlock::iterator MI, E;
    tie(BB, MI) = Worklist.pop_back_val();

    DEBUG(dbgs() << "Checking BB for redundancy:\n" <<*MI->getParent() <<'\n');

    // If we are revisiting StartBB, we scan to I to complete the cycle.
    // Otherwise we end at BB->begin().
    E = (BB == StartBB && MI == BB->end()) ? (MachineBasicBlock::iterator) I : BB->begin();

    // Scan for a load.  Terminate this path if we see one or a cut is
    // already forced.
    for (; MI != E; MI--)
    {
      DEBUG(dbgs() << "JVDW: Candidate: " << *MI << '\n');

      if(TII_->isIdemBoundary(MI) && DT_->dominates(MI, I))
      {
        // Delete I and return 
        DEBUG(dbgs() << "JVDW: Found redundant: " << *I <<'\n');
        DEBUG(dbgs() << *I->getParent() <<'\n');

        DEBUG(dbgs() << "\nJVDW: Found necessary: " << *MI <<'\n');
        DEBUG(dbgs() << *MI->getParent() <<'\n');

        return true;
      }

      if(MI->mayLoad())
      {

        DEBUG(dbgs() << "JVDW: Found non-redundant: " << *I <<'\n');
        DEBUG(dbgs() << *I->getParent() <<'\n');

        DEBUG(dbgs() << "JVDW: Found load: " << *MI <<'\n');
        DEBUG(dbgs() << *MI->getParent() <<'\n');
        return false;
      }
    }
      if(TII_->isIdemBoundary(E) && DT_->dominates(E, I))
      {
        // Delete I and return 
        DEBUG(dbgs() << "JVDW: Found redundant: " << *I <<'\n');
        DEBUG(dbgs() << *I->getParent() <<'\n');

        DEBUG(dbgs() << "\nJVDW: Found necessary: " << *E<<'\n');
        DEBUG(dbgs() << *MI->getParent() <<'\n');

        return true;
      }



    //// If the path didn't terminate, continue on to predecessors.
    //for (MachineBasicBlock::pred_iterator P = BB->pred_begin(); P != BB->pred_end(); ++P)
    //  if (Visited.insert(*P).second)
    //    Worklist.push_back(WorkItem((*P), (*P)->end()));

  } while (!Worklist.empty());

  // If we found an idem and exhausted our search without finding loads, delete
  //if ( FoundIdem && !FoundLoad )
  //{
  //  // Delete I an return
  //  I->eraseFromParent();
  //  return true;
  //}

  return false;
}

// For some unknown reason sometimes I find back to back checkpoints... it's
// useless! Get rid of one.
void MachineIdempotentRegions::removeDuplicates(MachineFunction &MF)
{
  // Remove obvious duplicates (one checkpoint after the other)
  for (MachineFunction::iterator B = MF.begin(), BE = MF.end(); B != BE; ++B)
  {
    MachineBasicBlock::iterator PrevI = B->begin();
    for (MachineBasicBlock::iterator I = B->begin(); I != B->end(); ++I)
    {
      if(PrevI != I && TII_->isIdemBoundary(PrevI) && TII_->isIdemBoundary(I))
      {
        I->eraseFromParent();
        I = B->begin();
      }
      PrevI = I;
    }
  }

  for (MachineFunction::iterator B = MF.begin(), BE = MF.end(); B != BE; ++B)
  {
    for (MachineBasicBlock::iterator I = B->begin(); I != B->end(); ++I)
    {
      if(TII_->isIdemBoundary(I))
        if (searchForPriorBoundaries(I))
        {
          I->eraseFromParent();
          I = B->begin();
        }
    }
  }
}

// Turn the IDEM intrinsic into an actuall checkpoint.
void MachineIdempotentRegions::lowerIdemToCheckpoint(MachineFunction &MF)
{
  for (MachineFunction::iterator B = MF.begin(), BE = MF.end(); B != BE; ++B)
    for (MachineBasicBlock::iterator I = B->begin(); I != B->end(); ++I)
      if (TII_->isIdemBoundary(I))
        TII_->emitCheckpoint(*(I->getParent()), I);

  // Remove idem boundaries
  for (MachineFunction::iterator B = MF.begin(), BE = MF.end(); B != BE; ++B)
    for (MachineBasicBlock::iterator I = B->begin(); I != B->end(); ++I)
      if (TII_->isIdemBoundary(I))
      {
        I->eraseFromParent();
        I = B->begin();
      }
}


void MachineIdempotentRegions::findAntidependencePairs(MachineInstr *MI) {
  DEBUG(dbgs() << "JVDW: Analyzing possible spill " << *MI << "\n");

  // Perform a reverse depth-first search to find aliasing loads.
  typedef std::pair<MachineBasicBlock *, MachineBasicBlock::iterator> WorkItem;
  SmallVector<WorkItem, 8> Worklist;
  SmallPtrSet<MachineBasicBlock *, 32> Visited;

  MachineBasicBlock *SpillBB = MI->getParent();
  Worklist.push_back(WorkItem(SpillBB, MI));
  do {
    MachineBasicBlock *BB;
    MachineBasicBlock::iterator I, E;
    tie(BB, I) = Worklist.pop_back_val();

    // If we are revisiting WriteBB, we scan to Write to complete the cycle.
    // Otherwise we end at BB->begin().
    E = (BB == SpillBB && I == BB->end()) ? (MachineBasicBlock::iterator) MI : BB->begin();

    // Scan for an aliasing load.  Terminate this path if we see one or a cut is
    // already forced.
    int FI;
    if(TII_->isStoreToStackSlot(MI, *&FI))
    {
      if (scanForAliasingLoad(MI, I, E, FI))
        continue;
    //}else if (!MI->memoperands_empty()) {
    //  if(scanForAliasingLoad(MI, I, E))
    //    continue;
    }

    // If the path didn't terminate, continue on to predecessors.
    for (MachineBasicBlock::pred_iterator P = BB->pred_begin(); P != BB->pred_end(); ++P)
      if (Visited.insert(*P).second)
        Worklist.push_back(WorkItem((*P), (*P)->end()));

  } while (!Worklist.empty());
}

bool MachineIdempotentRegions::scanForAliasingLoad(MachineInstr *Store,
                                                   MachineBasicBlock::iterator I,
                                                   MachineBasicBlock::iterator E,
                                                   int FI) {

  while (I != E) {
    --I;
    // If we see a forced cut, the path is already cut; don't scan any further.
    if (TII_->isIdemBoundary(I) || I->isCall())
      return true;

    // Otherwise, check for an aliasing load.
    int tFI;
    if (TII_->isLoadFromStackSlot(I, *&tFI))
    {
      DEBUG(dbgs() << "\tJVDW: comparing to " << *I << "\n");
      if (FI == tFI)
      {
        createRegionBefore(Store->getParent(), Store);
        return true;
      }
    }
  }

  return false;
}

bool MachineIdempotentRegions::scanForAliasingLoad(MachineInstr *Store,
                                                   MachineBasicBlock::iterator I,
                                                   MachineBasicBlock::iterator E) {

  while (I != E) {
    --I;
  
    DEBUG(dbgs() << "\t" << *I );

    // If we see a forced cut, the path is already cut; don't scan any further.
    if (TII_->isIdemBoundary(I) || I->isCall())
      return true;


    // This whole function is implemented very poorly. The only point is to
    // find those idempotency violations that seem to slip through the cracks
    // and make it to the back end. It expects ARM::tLDRi instructions and just
    // compares to see if the register and offset are the same.
    if (Store->getNumOperands() < 3)
      continue;
    if (!Store->getOperand(1).isReg() || !Store->getOperand(2).isImm())
      continue;

    // Otherwise, check for an aliasing load.
    if(I->mayLoad()) //&& !I->memoperands_empty())
    {
      if (I->getNumOperands() < 3)
        continue;
      if (!I->getOperand(1).isReg() || !I->getOperand(2).isImm())
        continue;
      if(I->getOperand(1).getReg() == Store->getOperand(1).getReg() && I->getOperand(2).getImm() == Store->getOperand(2).getImm())
      {
        DEBUG(dbgs() << "JVDW: Found pair \n");
        DEBUG(dbgs() << "\t" << *I << "\n");
        DEBUG(dbgs() << "\t" << *Store << "\n");
        createRegionBefore(Store->getParent(), Store);
        return true;
      }
    }
  }

  return false;
}


IdempotentRegion &MachineIdempotentRegions::createRegionAtBoundary(
    MachineInstr *MI) {
  assert(isRegionEntry(*MI) && "creating region at non-boundary");

  IdempotentRegion *Region =
    new (RegionAllocator_) IdempotentRegion(Regions_.size(), MI);
  Regions_.push_back(Region);
  assert(EntryToRegionMap_.insert(std::make_pair(MI, Region)).second &&
         "already in map");
  return *Region;
}

IdempotentRegion &MachineIdempotentRegions::createRegionBefore(
    MachineBasicBlock *MBB,
    MachineBasicBlock::iterator MI,
    SlotIndexes *Indexes) {

  // The new region starts at I.
  TII_->emitIdemBoundary(*MBB, MI);

  // Update Indexes as needed.
  MachineBasicBlock::iterator Boundary = (--MI);
  if (Indexes)
    Indexes->insertMachineInstrInMaps(Boundary);

  return createRegionAtBoundary(Boundary);
}

void MachineIdempotentRegions::getRegionsContaining(
  const MachineInstr &MI,
  SmallVectorImpl<IdempotentRegion *> *Regions) {

  // Clear the return argument.
  Regions->clear();

  // Walk the CFG backwards, starting at the instruction before MI.
  typedef std::pair<MachineBasicBlock::const_reverse_iterator,
                    const MachineBasicBlock *> WorkItemTy;
  SmallVector<WorkItemTy, 16> Worklist;
  Worklist.push_back(
      WorkItemTy(
          MachineBasicBlock::const_reverse_iterator(
              MachineBasicBlock::const_iterator(&MI)),
          MI.getParent()));

  SmallPtrSet<const MachineBasicBlock *, 32> Visited;
  do {
    MachineBasicBlock::const_reverse_iterator It;
    const MachineBasicBlock *MBB;
    tie(It, MBB) = Worklist.pop_back_val();

    // Look for a region entry or the block entry, whichever comes first. 
    while (It != MBB->rend() && !isRegionEntry(*It))
      It++;

    // If we found a region entry, add the region and skip predecessors.
    if (It != MBB->rend()) {
      Regions->push_back(&getRegionAtEntry(*It));
      continue;
    }

    // Examine predecessors.  Insert into Visited here to allow for a cycle back
    // to MI's block.
    for (MachineBasicBlock::const_pred_iterator P = MBB->pred_begin(),
         PE = MBB->pred_end(); P != PE; ++P)
      if (Visited.insert(*P).second)
        Worklist.push_back(WorkItemTy((*P)->rbegin(), *P));

  } while (!Worklist.empty());
}

#if 0
static void dumpVerifying(const MachineInstr &MI,
                          const DenseSet<unsigned> &LiveIns,
                          const SlotIndexes *Indexes,
                          const TargetRegisterInfo *TRI) {
  dbgs() << "For live-ins: [";
  for (DenseSet<unsigned>::const_iterator I = LiveIns.begin(),
       IE = LiveIns.end(), First = I; I != IE; ++I) {
    if (I != First)
      dbgs() << ", ";
    dbgs() << PrintReg(*I, TRI);
  }
  dbgs() << "], verifying instruction: ";
  if (Indexes)
    dbgs() << "\t" << Indexes->getInstructionIndex(&MI);
  dbgs() << "\t\t" << MI;
}
#endif

bool MachineIdempotentRegions::verifyInstruction(
    const MachineInstr &MI,
    const DenseSet<unsigned> &LiveIns,
    const SlotIndexes *Indexes) const {

  // Identity copies and kills don't really write to anything.
  if (MI.isIdentityCopy() || MI.isKill())
    return true;

  bool Verified = true;
  for (MachineInstr::const_mop_iterator O = MI.operands_begin(),
       OE = MI.operands_end(); O != OE; ++O)
    Verified &= verifyOperand(*O, LiveIns, Indexes);
  return Verified;
}

bool MachineIdempotentRegions::verifyOperand(
    const MachineOperand &MO,
    const DenseSet<unsigned> &LiveIns,
    const SlotIndexes *Indexes) const {
  unsigned Reg = 0;

  // For registers, consider only defs ignoring:
  //  - Undef defs, which are generated while RegisterCoalescer is running.
  //  - Implicit call defs.  They are handled by an idempotence boundary at the
  //    entry of the called function.
  if (MO.isReg() && MO.isDef() &&
      !(MO.isUndef() && MO.getParent()->isCopyLike()) &&
      !(MO.isImplicit() && MO.getParent()->isCall())) {
    Reg = MO.getReg();
    // Alse ignore:
    //  - Stack pointer defs; assume the SP is checkpointed at idempotence
    //    boundaries.
    //  - Condition code defs; assume the CCR is checkpointed at idempotence
    //    boundaries.  The SelectionDAG scheduler currently allows a CCR to be
    //    live across a boundary (could fix that instead).
    //  - Other target-specific special registers that are hard to handle.
    if (TargetRegisterInfo::isPhysicalRegister(Reg) &&
        TRI_->isProtectedRegister(Reg))
      return true;
  }

  // For frame indicies, consider only spills (stores, index > 0) for now.
  if (MO.isFI() && MO.getParent()->mayStore() && MO.getIndex() > 0)
    Reg = TargetRegisterInfo::index2StackSlot(MO.getIndex());

  // If Reg didn't get set, assume everything is fine.
  if (!Reg)
    return true;

  bool Verified = !LiveIns.count(Reg);
  if (!Verified) {
    errs() << PrintReg(Reg, TRI_) << " CLOBBER in:";
    if (Indexes)
      errs() << "\t" << Indexes->getInstructionIndex(MO.getParent());
    errs() << "\t\t" << *MO.getParent();
  }
  return Verified;
}

void MachineIdempotentRegions::print(raw_ostream &OS, const Module *) const {
  OS << "\n*** MachineIdempotentRegions: ***\n";
  for (const_iterator R = begin(), RE = end(); R != RE; ++R) {
    OS << **R << "\n";
  }
}


