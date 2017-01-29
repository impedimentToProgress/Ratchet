//===-------- MemoryIdempotenceAnalysis.cpp ---------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// PatchMachineIdempotentRegions takes the IR-level idempotent region
// construction out of SSA and prepares it for register and stack allocation.
// It performs a number of tasks:
//  (1) It patches the region construction to meet calling convention
//      constraints by placing boundaries at the function entry and function
//      call return points.  These approximate the points where stack pointer
//      updates occur, which effectively "commit" any call stack modifications.
//  (2) If requested, it inserts region boundaries before branches to enable
//      minimizing branch mis-prediction recovery (a slightly orthogonal issue
//      to "patching", but handled by this pass for convenience).
//  (3) It patches loops -- both natural and unnatural loops.  This is by far
//      the most complex and involved job of this pass.  We need to make sure
//      that (a) loops with region boundaries have at least two region
//      boundaries along all paths through the loop, and that (b) all clobbers
//      along the back-edges of loops are avoided by placing copies as needed.
//      See code for details.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "patch-machine-idempotent-regions"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/IdempotenceOptions.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineIdempotentRegions.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/MachineSSAUpdater.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/SlotIndexes.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Target/TargetInstrInfo.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Target/TargetRegisterInfo.h"
#include "IdempotenceUtils.h"

using namespace llvm;

namespace {
  class UseToRewrite;
  class PatchMachineIdempotentRegions : public MachineFunctionPass {
   public:
    static char ID;
    PatchMachineIdempotentRegions() : MachineFunctionPass(ID) {
      initializePatchMachineIdempotentRegionsPass(
          *PassRegistry::getPassRegistry());
    }

    virtual void getAnalysisUsage(AnalysisUsage &AU) const;
    virtual void releaseMemory();
    virtual bool runOnMachineFunction(MachineFunction &MF);

   private:
    typedef std::pair<MachineBasicBlock *, MachineBasicBlock *> Edge;
    typedef SmallVector<IdempotentRegion *, 4> Regions;

    // Non-natural loop retreating edges.
    typedef SmallVector<Edge, 4> NonNaturalLoopEdges;
    NonNaturalLoopEdges NonNaturalLoopEdges_;

    // Loops that need to be patched.
    typedef SmallPtrSet<MachineLoop *, 16> LoopsToPatch;
    LoopsToPatch LoopsToPatch_;

    // Mapping of natural loop back-edges to regions that dominate the latch in
    // dominating order.
    typedef std::map<Edge, Regions> BackEdgeToDomRegionsMap;
    BackEdgeToDomRegionsMap BackEdgeToDomRegionsMap_;

    // Cached loop information.
    MachineLoop *CurLoop_;
    SmallVector<MachineBasicBlock *, 32> CurLoopBlocks_;
    SmallVector<MachineBasicBlock *, 8> CurLoopExits_;
    SmallVector<MachineBasicBlock *, 8> CurLoopExiting_;

    // The usual.
    MachineFunction          *MF_;
    MachineIdempotentRegions *MIR_;
    MachineLoopInfo          *MLI_;
    MachineDominatorTree     *MDT_;
    MachineRegisterInfo      *MRI_;
    const TargetInstrInfo    *TII_;
    const TargetRegisterInfo *TRI_;

    // Patch function entry and call return sites with idempotence cuts.
    // - Function entry:  Without a cut at the function entry,  we must assume 
    //   arguments held in physical registers are live across the current region
    //   started in the calling function.  Hence, we could not push these
    //   argument to the stack and re-use the registers (bad for performance).
    //   We also could not overwrite them with function return values
    //   (bad for correctness).
    // - Call return sites:  Effectively the same arguments apply, but for
    //   return registers and the stack state of the returning function instead
    //   of argument registers.
    void patchCallingConvention();
    void createRegionForCall(MachineBasicBlock *MBB,
                             MachineBasicBlock::iterator I);

    // Patch control divergence for fast branch misprediction recovery.
    void patchControlDivergence();

    // Patch non-natural loops to ensure that a cycle crosses at least two cuts.
    void patchNonNaturalLoops();
    bool analyzeNonNaturalLoopEdges();
    std::pair<IdempotentRegion *, IdempotentRegion *>
      patchUnanalyzableLoopEdge(Edge *LoopEdge);
    void copyOutPHIBefore(MachineInstr *PHI,
                          MachineBasicBlock *MBB,
                          MachineBasicBlock::iterator CopyOutBefore);

    // Intelligently patch natural loops to ensure that a cycle crosses at least
    // two cuts.  Consists of two parts:
    // - patchLoopAndSubloopDominatingRegions() makes sure each loop containing
    //   at least one cut has at least two cuts that are crossed through all
    //   possible paths through the loop.
    // - patchLoopAndSubloopBackEdgeClobbers() inserts copies to ensure that
    //   variables live at the entry point of the region and spanning a
    //   back-edge of the loop are not clobbered by a definition after the
    //   back-edge.
    void patchNaturalLoops();
    void patchLoopAndSubloopDominatingRegions(MachineLoop *Loop);
    void patchLoopDominatingRegions(MachineLoop *Loop);
    void patchLoopAndSubloopBackEdgeClobbers(MachineLoop *Loop, bool *Repeat);
    void patchLoopBackEdgeClobbers(MachineLoop *Loop, bool *Repeat);

    // Analysis routines to remember and update information about loops
    // containing at least one cut.
    void analyzeLoopsContainingRegions();
    bool analyzeLoopsContainingRegion(IdempotentRegion *Region);

    // Helper routines for patchLoopBackEdgeClobbers().
    void processBackEdgeDef(const Regions &BackEdgeRegion,
                            MachineOperand *Def,
                            bool *Repeat);
    UseToRewrite buildUseToRewrite(const Regions &BackEdgeRegions,
                                   MachineOperand *DefMO,
                                   MachineOperand *UseMO) const;
    IdempotentRegion *getDominatingRegion(const MachineInstr &UseMI, 
                                          const MachineBasicBlock &UseMBB, 
                                          const Regions &BackEdgeRegions) const;

    // Fast access to information about the loop currently being processed by
    // patchLoopBackEdgeClobbers().
    void clearCurLoopInfo();
    void setCurLoopInfo(MachineLoop *Loop);
    bool isCurLoopHeader(const MachineBasicBlock &MBB) const;
    bool isCurLoopBlock(const MachineBasicBlock &MBB) const;
    bool isCurLoopExit(const MachineBasicBlock &MBB) const;

    // Return the basic blocks that are latches of Loop.
    void getLoopBackEdges(const MachineLoop &Loop,
                          SmallVectorImpl<Edge> *BackEdges) const {
      MachineBasicBlock *Header = Loop.getHeader();
      for (MachineBasicBlock::const_pred_iterator P = Header->pred_begin(),
           PE = Header->pred_end(); P != PE; ++P)
        if (Loop.contains(*P))
          BackEdges->push_back(Edge(*P, Header));
    }
  };
} // end anonymous namespace

char PatchMachineIdempotentRegions::ID = 0;
char &llvm::PatchMachineIdempotentRegionsID = PatchMachineIdempotentRegions::ID;

INITIALIZE_PASS_BEGIN(PatchMachineIdempotentRegions,
                      "patch-machine-idempotence-regions",
                      "Patch Machine Idempotent Regions", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTree)
INITIALIZE_PASS_DEPENDENCY(MachineIdempotentRegions)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfo)
INITIALIZE_PASS_DEPENDENCY(UnreachableMachineBlockElim)
INITIALIZE_PASS_END(PatchMachineIdempotentRegions,
                    "patch-machine-idempotence-regions",
                    "Patch Machine Idempotent Regions", false, false)

FunctionPass *llvm::createPatchMachineIdempotentRegionsPass() {
  return new PatchMachineIdempotentRegions();
}

void PatchMachineIdempotentRegions::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<MachineIdempotentRegions>();
  AU.addRequired<MachineLoopInfo>();
  AU.addRequired<MachineDominatorTree>();
  AU.addPreserved<MachineIdempotentRegions>();
  AU.addPreserved<MachineLoopInfo>();
  AU.addPreserved<MachineDominatorTree>();
  AU.addPreservedID(UnreachableMachineBlockElimID);
  MachineFunctionPass::getAnalysisUsage(AU);
}

void PatchMachineIdempotentRegions::releaseMemory() {
  NonNaturalLoopEdges_.clear();
  LoopsToPatch_.clear();
  BackEdgeToDomRegionsMap_.clear();
  //clearCurLoopInfo();
}

bool PatchMachineIdempotentRegions::runOnMachineFunction(MachineFunction &MF) {
  DEBUG(dbgs() << "********** PATCH MACHINE IDEMPOTENT REGIONS **********\n");
  assert(IdempotenceConstructionMode != IdempotenceOptions::NoConstruction &&
         "pass should not be run");

  MF_  = &MF;
  MDT_ = &getAnalysis<MachineDominatorTree>();
  MIR_ = &getAnalysis<MachineIdempotentRegions>();
  MLI_ = &getAnalysis<MachineLoopInfo>();
  MRI_ = &MF.getRegInfo();
  TII_ = MF.getSubtarget().getInstrInfo();
  TRI_ = MF.getSubtarget().getRegisterInfo();
  
  // This pass patches machine idempotent regions while still in SSA form so
  // that register allocation can proceed without producing any clobbers.
  assert(MRI_->isSSA() && "not in SSA");
  MRI_->setPatched();

  //// Patch the calling convention, inserting cuts around call sites.
  //patchCallingConvention();

  //// Patch control divergence if requested.
  //if (IdempotenceConstructionMode == IdempotenceOptions::BranchRecovery)
  //  patchControlDivergence();

  //// Patch non-natural loops quickly, if they exist.
  //patchNonNaturalLoops();

  //// Patch natural loops with more care.
  //patchNaturalLoops();

  return true;
}

// *** patchCallingConvention *********************************************** //

//static bool isFrameMI(const MachineInstr &MI) {
//  for (MachineInstr::const_mop_iterator O = MI.operands_begin(),
//       OE = MI.operands_end(); O != OE; ++O)
//    if ((*O).isFI())
//      return true;
//  return false;
//}
//
//void PatchMachineIdempotentRegions::patchCallingConvention() {
//  DEBUG(dbgs() << "\nPatching calling convention\n");
//  MachineBasicBlock *EntryMBB = MF_->begin();
//  for (MachineFunction::iterator MBB = MF_->begin(), MBBE = MF_->end();
//       MBB != MBBE; ++MBB) {
//
//    // Handle function entry.
//    if (EntryMBB == &*MBB) {
//      DEBUG(dbgs() << " Patching entry BB#" << MBB->getNumber() << "\n");
//
//      // Skip base register formation from frame index for ARM
//      MachineBasicBlock::iterator I = MBB->begin();
//      while (I != MBB->end() && isFrameMI(*I))
//        ++I;
//      createRegionForCall(MBB, I);
//    }
//
//    // Handle call returns.
//    for (MachineBasicBlock::iterator I = MBB->begin(), IE = MBB->end(); I != IE;
//         ++I) {
//      if (!I->isCall())
//        continue;
//      DEBUG(dbgs() << " Patching call in BB#" << MBB->getNumber() << ": "
//            << *I);
//
//      // Ignore tail calls.
//      MachineBasicBlock::iterator Next = next(I);
//      if (Next == IE)
//        continue;
//
//      while (Next->getOpcode() == TII_->getCallFrameDestroyOpcode())
//        ++Next;
//      createRegionForCall(MBB, Next);
//    }
//  }
//}
//
//void PatchMachineIdempotentRegions::createRegionForCall(
//    MachineBasicBlock *MBB,
//    MachineBasicBlock::iterator I) {
//
//  // Advance past argument copies and kills.
//  MachineBasicBlock::iterator IE = MBB->end();
//  for (; I != IE && (I->isCopy() || I->isKill()); ++I) {
//    unsigned SrcReg = I->getOperand(1).getReg();
//    if (!TargetRegisterInfo::isPhysicalRegister(SrcReg))
//      break;
//    assert(!isCalleeSavedRegister(SrcReg, *TRI_));
//  }
//
//  // Exceptions are really annoying.
//  if (I->isEHLabel()) {
//    for (MachineBasicBlock::succ_iterator S = MBB->succ_begin(),
//         SE = MBB->succ_end(); S != SE; ++S) {
//      MachineBasicBlock *Succ = *S;
//      MachineBasicBlock::iterator J = Succ->begin(), JE = Succ->end();
//      // Skip PHIs, copies, kills, and EH labels.
//      while (J != JE &&
//             (J->isPHI() || J->isCopy() || J->isKill() || J->isEHLabel()))
//        ++J;
//      if (!MIR_->isRegionEntry(*J))
//        MIR_->createRegionBefore(Succ, J);
//    }
//  } else {
//    if (!MIR_->isRegionEntry(*I))
//      MIR_->createRegionBefore(MBB, I);
//  }
//}
//
//// *** patchControlDivergence *********************************************** //
//
//static bool hasLandingPadSuccessor(const MachineBasicBlock &MBB) {
//  for (MachineBasicBlock::const_succ_iterator S = MBB.succ_begin(),
//       SE = MBB.succ_end(); S != SE; ++S)
//    if ((*S)->isLandingPad())
//      return true;
//  return false;
//}
//
//void PatchMachineIdempotentRegions::patchControlDivergence() {
//  DEBUG(dbgs() << "\nPatching control divergence\n");
//  for (MachineFunction::iterator MBB = MF_->begin(), MBBE = MF_->end();
//       MBB != MBBE; ++MBB) {
//
//    // Only consider basic blocks with multiple possible successors.  Also
//    // ignore blocks with invoke terminators.  They were already dealt with by
//    // patchCallingConvention().
//    if (MBB->succ_size() < 2 || hasLandingPadSuccessor(*MBB))
//      continue;
//
//    // Also skip if there is already a boundary before the terminator.
//    MachineBasicBlock::iterator Term = MBB->getFirstTerminator();
//    assert(Term != MBB->end());
//    if (Term != MBB->begin() && MIR_->isRegionEntry(*(Term--)))
//      continue;
//
//    DEBUG(dbgs() << " Patching BB#" << MBB->getNumber() << "\n");
//    MIR_->createRegionBefore(MBB, Term);
//  }
//}
//
//// *** patchNonNaturalLoops ************************************************* //
//
//void PatchMachineIdempotentRegions::patchNonNaturalLoops() {
//  // For each retreating edge that forms a non-natural loop, insert boundaries
//  // at both edge nodes.  This guarantees at least two boundary crossings on
//  // the retreating edge and no back-edge clobbers (see patchNaturalLoops()).
//  // This is slightly pessimistic, but non-natural loops should be rare enough
//  // that the impact is negligible.
//  if (analyzeNonNaturalLoopEdges()) {
//    DEBUG(dbgs() << "\nPatching " << NonNaturalLoopEdges_.size()
//          << " non-natural loops\n");
//    for (NonNaturalLoopEdges::iterator I = NonNaturalLoopEdges_.begin(),
//         IE = NonNaturalLoopEdges_.end(); I != IE; ++I)
//      patchUnanalyzableLoopEdge(&*I);
//  }
//}
//
//bool PatchMachineIdempotentRegions::analyzeNonNaturalLoopEdges() {
//#ifndef NDEBUG
//  SmallPtrSet<MachineLoop *, 16> NaturalLoops;
//#endif
//  assert(NonNaturalLoopEdges_.empty());
//  
//  // Use a reverse post-order traversal (AKA depth-first ordering) to find all
//  // loops and prune out the natural loops.
//  ReversePostOrderTraversal<MachineBasicBlock *> RPOT(MF_->begin());
//  SmallPtrSet<MachineBasicBlock *, 32> Visited;
//  for (ReversePostOrderTraversal<MachineBasicBlock *>::rpo_iterator
//       I = RPOT.begin(), IE = RPOT.end(); I != IE; ++I) {
//    MachineBasicBlock *MBB = *I;
//    assert(Visited.insert(MBB).second);
//
//    // Check for successors that we've already seen.
//    for (MachineBasicBlock::succ_iterator S = MBB->succ_begin(),
//         SE = MBB->succ_end(); S != SE; ++S) {
//      MachineBasicBlock *Succ = *S;
//      if (!Visited.count(Succ))
//        continue;
//
//      // Prune out retreating edges that are back-edges of a natural loop.
//      MachineLoop *Loop = MLI_->getLoopFor(Succ);
//      if (Loop && Loop->getHeader() == Succ) {
//#ifndef NDEBUG
//        NaturalLoops.insert(Loop);
//#endif
//        continue;
//      }
//
//      Edge RetreatingEdge = Edge(MBB, Succ);
//      NonNaturalLoopEdges_.push_back(RetreatingEdge);
//    }
//  }
//
//#ifndef NDEBUG
//  // Check that all retreating edges that did not form non-natural loops are
//  // accounted for by MachineLoopInfo.
//  SmallVector<MachineLoop *, 8> Worklist;
//  for (MachineLoopInfo::iterator L = MLI_->begin(); L != MLI_->end(); ++L)
//    Worklist.push_back(*L);
//
//  Visited.clear();
//  while (!Worklist.empty()) {
//    MachineLoop *Loop = Worklist.pop_back_val();
//    for (MachineLoop::iterator L = Loop->begin(); L != Loop->end(); ++L)
//      Worklist.push_back(*L);
//
//    assert(Visited.insert(Loop->getHeader()).second && "header of multiple loops");
//    assert(NaturalLoops.count(Loop) && "loop not found in RPO traversal");
//  }
//#endif
//  return !NonNaturalLoopEdges_.empty();
//}
//
//std::pair<IdempotentRegion *, IdempotentRegion *>
//PatchMachineIdempotentRegions::patchUnanalyzableLoopEdge(Edge *LoopEdge) {
//  std::pair<IdempotentRegion *, IdempotentRegion *> Result;
//
//  MachineBasicBlock *Tail, *Head;
//  std::tie(Tail, Head) = *LoopEdge;
//  DEBUG(dbgs() << " Patching non-analyzable retreating edge BB#"
//        << Tail->getNumber() << "->BB#" << Head->getNumber() << "\n");
//
//  // Insert a region boundary at the first place we can in Head.
//  MachineBasicBlock::iterator HeadFirstNonPHI = Head->getFirstNonPHI();
//  Result.first = &MIR_->createRegionBefore(Head, HeadFirstNonPHI);
//
//  // Either ensure that the Tail->Head edge is non-critical, or make sure that
//  // all values defined before HeadFirstNonPHI in Head are copied out so they
//  // cannot be clobbered along the retreating edge.
//  assert(Head->pred_size() > 1 && "successor has no other predecessor");
//  if (Tail->succ_size() > 1) {
//    // Edge is critical.  Attempt split.
//    DEBUG(dbgs() << "  Splitting back-edge critical edge\n");
//    Tail = Tail->SplitCriticalEdge(Head, this);
//
//    // If the split failed, copy out all PHI values in Head immediately after
//    // the region boundary we just inserted.  The RegisterCoalescer will undo
//    // some of this if it finds it safe to eliminate some of the copies.
//    if (!Tail) {
//      DEBUG(dbgs() << "   Cannot be split, copying out PHIs in Head\n");
//      for (MachineBasicBlock::iterator I = Head->begin(); I->isPHI(); ++I) 
//        copyOutPHIBefore(I, Head, HeadFirstNonPHI);
//
//      // Recover Tail.
//      Tail = LoopEdge->first;
//    }
//  }
//  
//  // Now insert a region boundary in Tail if there isn't already one there.
//  MachineBasicBlock::iterator I = Tail->begin(), E = Tail->end();
//  while (I != E && !MIR_->isRegionEntry(*I))
//    ++I;
//  Result.second = (I == E) ?
//    &MIR_->createRegionBefore(Tail, Tail->getFirstNonPHI()) :
//    &MIR_->getRegionAtEntry(*I);
//
//  return Result;
//}
//
//void PatchMachineIdempotentRegions::copyOutPHIBefore(
//    MachineInstr *PHI,
//    MachineBasicBlock *MBB,
//    MachineBasicBlock::iterator CopyOutBefore) {
//
//  MachineOperand *PHIDef = &PHI->getOperand(0);
//  unsigned PHIVReg = PHIDef->getReg();
//  unsigned CopyVReg = MRI_->createVirtualRegister(MRI_->getRegClass(PHIVReg));
//
//  // First rewrite uses of PHIReg to use CopyVReg.
//  typedef SmallVector<MachineOperand *, 8> UsesToRewriteTy;
//  UsesToRewriteTy UsesToRewrite;
//  for (MachineRegisterInfo::use_nodbg_iterator
//       U = MRI_->use_nodbg_begin(PHIVReg),
//       UE = MRI_->use_nodbg_end(); U != UE; ++U)
//    UsesToRewrite.push_back(&U);
//    //UsesToRewrite.push_back(&U.getOperand());
//
//  // Rewriting messes up use_nodbg_iterator so rewrite in separate phase.
//  for (UsesToRewriteTy::iterator I = UsesToRewrite.begin(),
//       E = UsesToRewrite.end(); I != E; ++I) 
//    (*I)->setReg(CopyVReg);
//
//  // Create the copy to feed all those uses.
//  BuildMI(*MBB, CopyOutBefore,
//          PHI->getDebugLoc(),
//          TII_->get(TargetOpcode::COPY),
//          CopyVReg).addReg(PHIVReg, 0, PHIDef->getSubReg());
//  DEBUG(dbgs() << "  Created copy to " << PrintReg(CopyVReg, TRI_)
//        << " from " << PrintReg(PHIVReg, TRI_) << " for PHI " << *PHI);
//}
//
//// *** patchNaturalLoops **************************************************** //
//
//void PatchMachineIdempotentRegions::patchNaturalLoops() {
//  // Pre-compute a mapping from loops to regions contained in those loops.
//  analyzeLoopsContainingRegions();
//
//  // Patch loops to make sure that there are at least two regions whose entry
//  // dominate each loop latch for all loops that contain regions.
//  for (MachineLoopInfo::iterator L = MLI_->begin(); L != MLI_->end(); ++L)
//    patchLoopAndSubloopDominatingRegions(*L);
//
//  // Patch loop variables defined in regions that run across back-edges of
//  // the loop to make sure definitions after the back-edge do not clobber uses
//  // before the back-edge in those regions.  SSA doesn't provide for this
//  // case so we have to do it ourselves.  Here is an example:
//  //
//  // loop: {
//  //   x = phi(y, z)
//  //   [region boundary]
//  //   [region boundary]
//  //   z = x + 1
//  // }
//  //
//  // Here the assignment to x clobbers the use of x in the region that crosses
//  // the back-edge of the loop.  We basically insert copies to avoid this.
//  // The algorithm is complex, however, to accommodate the general case, and at
//  // each step it can have potentially non-local effects that force us to repeat
//  // until we hit a fixed point.
//  bool Repeat;
//  do {
//    Repeat = false;
//    for (MachineLoopInfo::iterator L = MLI_->begin(); L != MLI_->end(); ++L)
//      patchLoopAndSubloopBackEdgeClobbers(*L, &Repeat);
//  } while (Repeat);
//}
//
//namespace {
//  // Helper class used to sort back-edge dominating regions in dominance order.
//  class CompareRegionDominance :
//      public std::binary_function<IdempotentRegion *,
//                                  IdempotentRegion *, bool> {
//   public:
//    CompareRegionDominance(MachineDominatorTree *MDT) : MDT_(MDT) {};
//    bool operator()(const IdempotentRegion *Left,
//                    const IdempotentRegion *Right) const {
//      return MDT_->dominates(&Left->getEntry(), &Right->getEntry());
//    }
//   private:
//    MachineDominatorTree *MDT_;
//  };
//}
//
//void PatchMachineIdempotentRegions::analyzeLoopsContainingRegions() {
//  for (MachineIdempotentRegions::iterator R = MIR_->begin(), RE = MIR_->end();
//       R != RE; ++R)
//    analyzeLoopsContainingRegion(*R);
//}
//
//bool PatchMachineIdempotentRegions::analyzeLoopsContainingRegion(
//    IdempotentRegion *Region) {
//
//  bool NewLoopsToPatch = false;
//  MachineBasicBlock *REntry = &Region->getEntryMBB();
//  for (MachineLoop *L = MLI_->getLoopFor(REntry); L; L = L->getParentLoop()) {
//
//    // Update BackEdgeToDomRegionsMap_ for each back-edge Region dominates.
//    SmallVector<Edge, 4> BackEdges;
//    getLoopBackEdges(*L, &BackEdges);
//    for (SmallVectorImpl<Edge>::iterator E = BackEdges.begin(),
//         EE = BackEdges.end(); E != EE; ++E) {
//      Edge &BackEdge = *E;
//      Regions &DominatingRegions = BackEdgeToDomRegionsMap_[BackEdge];
//      MachineBasicBlock *Latch = BackEdge.first;
//
//      // If the region entry dominates the latch, insert in dominance order
//      // position (dominates all first).
//      if (MDT_->dominates(REntry, Latch)) {
//        Regions::iterator It = std::upper_bound(DominatingRegions.begin(),
//                                                DominatingRegions.end(),
//                                                Region,
//                                                CompareRegionDominance(MDT_));
//        DominatingRegions.insert(It, Region);
//      }
//    }
//
//    // This loop contains at least one region boundary and needs at least two
//    // dominating boundaries along all back-edges.
//    NewLoopsToPatch |= LoopsToPatch_.insert(L);
//  }
//  return NewLoopsToPatch;
//}
//
//// Process loops from inner to outer loops (post-order).
////  - Patched dominating regions affect outer loops. 
////  - Patched dominating regions affect inner loops only if the patching
////    inserted regions in the inner loop.  If that happens, revisit inner loops.
//void PatchMachineIdempotentRegions::patchLoopAndSubloopDominatingRegions(
//    MachineLoop *Loop) {
//
//  // First children.
//  for (MachineLoop::iterator L = Loop->begin(); L != Loop->end(); ++L)
//    patchLoopAndSubloopDominatingRegions(*L);
//
//  // Then patch.
//  if (LoopsToPatch_.count(Loop))
//    patchLoopDominatingRegions(Loop);
//}
//
//void PatchMachineIdempotentRegions::patchLoopDominatingRegions(
//    MachineLoop *Loop) {
//  DEBUG(dbgs() << "\nPatching dominating regions for " << *Loop);
//
//  // Make sure each back-edge is dominated by at least two idempotence cuts.
//  SmallVector<Edge, 4> BackEdges;
//  getLoopBackEdges(*Loop, &BackEdges);
//  for (SmallVectorImpl<Edge>::iterator E = BackEdges.begin(),
//       EE = BackEdges.end(); E != EE; ++E) {
//    Edge &BackEdge = *E;
//    MachineBasicBlock *Latch, *Header;
//    tie(Latch, Header) = BackEdge;
//
//    // Get the dominating regions vector for this back-edge.
//    Regions *DominatingRegions = &BackEdgeToDomRegionsMap_[BackEdge];
//    DEBUG(dbgs() << " Examining back-edge BB#" << Latch->getNumber()
//          << "->BB#" << Header->getNumber() << " with "
//          << DominatingRegions->size() << " dominating regions\n");
//
//    // The last dominating region is the one that most immediately dominates the
//    // back-edge and hence is a region that contains the back-edge.  This region
//    // is special because it can have clobbers independent of SSA.
//    MachineBasicBlock *BackEdgeRegionEntry = NULL;
//    if (!DominatingRegions->empty()) {
//      IdempotentRegion *BackEdgeRegion = DominatingRegions->back();
//      DEBUG(dbgs() << "  Back-edge region is " << *BackEdgeRegion << "\n");
//      BackEdgeRegionEntry = &BackEdgeRegion->getEntryMBB();
//    }
//
//    // We place a cut at the loop latch if:
//    // - No dominating regions:  In this case, the policy is to place on cut at
//    //   the latch and one at the header to form at least two dominating
//    //   regions.
//    // - One dominating region at the header:  If there is already one
//    //   dominating region and it starts at the header, form the complement
//    //   region at the latch by placing a second cut there.  If the one
//    //   dominating region is not at the header, we prefer to place at the
//    //   header, so we skip the latch.
//    // - The back-edge dominating region does not start in this loop:  If the
//    //   most inner loop of BackEdgeRegion's entry is not this loop, we can
//    //   get the situation where this loop defines a value that is used in the
//    //   inner loop, and because the value must be live across inner loop
//    //   iterations, the value is necessarily live-out of BackEdgeRegion no
//    //   matter what we do in patchLoopBackEdgeClobbers() and the value may be
//    //   clobbered along this loop's back-edge if/when the value is redefined in
//    //   this loop.
//    if (DominatingRegions->empty() ||
//        (DominatingRegions->size() == 1 && BackEdgeRegionEntry == Header) ||
//        MLI_->getLoopFor(BackEdgeRegionEntry) != Loop) {
//
//      // We are going to cut at the latch, but make sure the latch's inner-most
//      // loop corresponds with this loop.  If it doesn't, we may still get the
//      // situation where we have a back-edge of a variable live in an inner
//      // loop.  To make this loop the latch's inner-most loop, split the
//      // critical edge between the latch and the header.  In general, splitting
//      // loop back-edges is bad for code placement (see comment in
//      // PHIElimination::SplitPHIEdges()),  but since this is not the inner-most
//      // loop, it is probably fine.
//      if (MLI_->getLoopFor(Latch) != Loop) {
//        DEBUG(dbgs() << "  Splitting Latch->Header critical edge\n");
//        assert(Header->pred_size() > 1 && Latch->succ_size() > 1 &&
//               "not a critical edge");
//        Latch = Latch->SplitCriticalEdge(Header, this);
//
//        // If splitting didn't work, do some really pessimistic stuff.
//        if (!Latch) {
//          DEBUG(dbgs() << "   Failed. Falling back to worst case\n");
//          std::pair<IdempotentRegion *, IdempotentRegion *> Pair =
//            patchUnanalyzableLoopEdge(&BackEdge);
//          bool NewLoopsToPatch = analyzeLoopsContainingRegion(Pair.first);
//          assert(!NewLoopsToPatch && "insert at header affected child loops");
//
//          // The latch insertion may have affected child loops.
//          if (analyzeLoopsContainingRegion(Pair.second)) {
//            DEBUG(dbgs() << "Revisiting child loops...\n");
//            for (MachineLoop::iterator L = Loop->begin(); L != Loop->end(); ++L)
//              patchLoopAndSubloopDominatingRegions(*L);
//          }
//          return;
//        }
//
//        // TODO:  Splitting the critical edge as-is is bad for sub-loop code
//        // placement -- it likely inserts the new latch right between the
//        // inner-loop's latch and its header.  Move the new latch before the
//        // outer-loop's (this loop's) header instead.  The tricky part is
//        // updating the terminators.
//        //
//        // Latch->moveBefore(Header);
//        // ...
//
//        // Migrate the dominating regions in the map.
//        Edge NewBackEdge(Latch, Header);
//        Regions *NewDominatingRegions = &BackEdgeToDomRegionsMap_[NewBackEdge];
//        std::swap(*NewDominatingRegions, *DominatingRegions);
//        BackEdgeToDomRegionsMap_.erase(BackEdge);
//
//        // Update running information.
//        BackEdge = NewBackEdge;
//        DominatingRegions = NewDominatingRegions;
//      }
//
//      // Use getFirstNonPHI() instead of getFirstTerminator() to make probable
//      // that the induction variable update is contained in the new region.
//      IdempotentRegion *Region =
//        &MIR_->createRegionBefore(Latch, Latch->getFirstNonPHI());
//      DEBUG(dbgs() << "  Inserted region " << *Region << " at loop latch BB#"
//            << Latch->getNumber() << "\n");
//      bool NewLoopsToPatch = analyzeLoopsContainingRegion(Region);
//      assert(!NewLoopsToPatch && "inserting at latch affected child loops");
//    }
//
//    // If we still don't have two dominating boundaries, putting a boundary at
//    // the header is ideal in terms of limiting shadows crossing into the loop.
//    if (DominatingRegions->size() < 2) {
//      IdempotentRegion *Region =
//        &MIR_->createRegionBefore(Header, Header->getFirstNonPHI());
//      DEBUG(dbgs() << "  Inserted region " << *Region << " at loop header BB#"
//            << Header->getNumber() << "\n");
//      bool NewLoopsToPatch = analyzeLoopsContainingRegion(Region);
//      assert(!NewLoopsToPatch && "inserting at header affected child loops");
//    }
//
//    // There must now be at least two back-edge dominating regions.
//    assert(DominatingRegions->size() >= 2);
//  }
//}
//
//// Process loops from outer to inner (pre-order). 
////  - Patched back-edge clobbers are assumed to affect inner loops. 
////  - Patched back-edge clobbers may affect other loops too if the patching
////    inserted one or more instructions outside the loop.  Unlikely but
////    possible.  If it happens, repeat.  That is what the Repeat flag is for.
//void PatchMachineIdempotentRegions::patchLoopAndSubloopBackEdgeClobbers(
//    MachineLoop *Loop, bool *Repeat) {
//
//  // First patch.
//  if (LoopsToPatch_.count(Loop))
//    patchLoopBackEdgeClobbers(Loop, Repeat);
//
//  // Then children.
//  for (MachineLoop::iterator L = Loop->begin(); L != Loop->end(); ++L)
//    patchLoopAndSubloopBackEdgeClobbers(*L, Repeat);
//}
//
//void PatchMachineIdempotentRegions::patchLoopBackEdgeClobbers(
//    MachineLoop *Loop, bool *Repeat) {
//  DEBUG(dbgs() << "\nPatching loop clobbered variables for " << *Loop);
//
//  // Cache current loop information.
//  setCurLoopInfo(Loop);
//  MachineBasicBlock *Header = Loop->getHeader();
//
//  // Compute the set of most-dominating regions that cross the loop back-edges.
//  // These regions will hold any generated copies.
//  Regions BackEdgeRegions;
//  SmallVector<Edge, 4> BackEdges;
//  getLoopBackEdges(*Loop, &BackEdges);
//  for (SmallVectorImpl<Edge>::iterator E = BackEdges.begin(),
//       EE = BackEdges.end(); E != EE; ++E) {
//    IdempotentRegion *BackEdgeRegion = BackEdgeToDomRegionsMap_[*E].back();
//
//    // Skip if BackEdgeRegion is already in BackEdgeRegions.
//    if (std::find(BackEdgeRegions.begin(), BackEdgeRegions.end(),
//                  BackEdgeRegion) != BackEdgeRegions.end()) {
//      DEBUG(dbgs() << " Skipping existing region " << *BackEdgeRegion << "\n");
//      continue;
//    }
//
//    // Skip if BackEdgeRegion is already dominated.
//    std::binder2nd<CompareRegionDominance> DominatesBackEdgeRegion(
//        CompareRegionDominance(MDT_), BackEdgeRegion);
//    if (std::find_if(BackEdgeRegions.begin(), BackEdgeRegions.end(),
//                     DominatesBackEdgeRegion) != BackEdgeRegions.end()) {
//      DEBUG(dbgs() << " Skipping dominated region " << *BackEdgeRegion << "\n");
//      continue;
//    }
//
//    // Remove any regions dominated by BackEdgeRegion (unlikely but possible).
//    std::binder1st<CompareRegionDominance> BackEdgeRegionDominates(
//        CompareRegionDominance(MDT_), BackEdgeRegion);
//    Regions::iterator It = std::remove_if(BackEdgeRegions.begin(),
//                                          BackEdgeRegions.end(),
//                                          BackEdgeRegionDominates);
//    if (It != BackEdgeRegions.end()) {
//      DEBUG(dbgs() << " Pruned " << (BackEdgeRegions.end() - It)
//            << " dominated regions \n");
//      BackEdgeRegions.resize(It - BackEdgeRegions.begin());
//    }
//
//    // Now add the region.
//    DEBUG(dbgs() << " Adding back-edge region " << *BackEdgeRegion << "\n");
//    BackEdgeRegions.push_back(BackEdgeRegion);
//  }
//
//  // Check for definitions in blocks that come after back-edges (i.e. after
//  // the header) and before any other region that starts after a back-edge.
//  // Iterating over any back-edge region starting at the loop header will find
//  // these definitions.  The code below uses the first back-edge region.
//  for (IdempotentRegion::mbb_iterator RI(*BackEdgeRegions.front(), Header);
//       RI.isValid(); ++RI) {
//    // Don't look at definitions outside of this loop.  Either they are handled
//    // at the outer-loop level or they can't clobber by virtue of SSA.
//    MachineBasicBlock *MBB = &RI.getMBB();
//    if (isCurLoopExit(*MBB)) {
//      RI.skip();
//      continue;
//    }
//
//    // Walk each def operand for each instruction in RI's range and compute
//    // uses of those defs whose shadows may be clobbered along the back-edge.
//    MachineBasicBlock::iterator I, IE;
//    for (tie(I, IE) = *RI; I != IE; ++I) {
//      for (MachineInstr::mop_iterator O = I->operands_begin(),
//           OE = I->operands_end(); O != OE; ++O) {
//        MachineOperand *MO = &*O;
//        if (!MO->isReg() || !MO->isDef())
//          continue;
//        unsigned VReg = MO->getReg();
//        if (!VReg || !TargetRegisterInfo::isVirtualRegister(VReg))
//          continue;
//
//        // Process this back-edge definition.  PHIs inserted outside this loop
//        // may affect other loops.  Be sure to check them again by setting the
//        // Repeat flag if such PHIs get inserted.
//        processBackEdgeDef(BackEdgeRegions, MO, Repeat);
//      }
//    }
//  }
//}
//
//namespace {
//  class UseToRewrite {
//   public:
//    // Construct a "no clobber" UseToRewrite.
//    UseToRewrite() : Operand_(NULL), DominatingRegion_(NULL) {}
//
//    // Construct a "may clobber" UseToRewrite of MO, with potentially many
//    // regions involved.
//    UseToRewrite(MachineOperand *MO) : Operand_(MO), DominatingRegion_(NULL) {}
//
//    // Construct a "must clobber" UseToRewrite of MO after region IR.
//    UseToRewrite(MachineOperand *MO, IdempotentRegion *IR)
//        : Operand_(MO), DominatingRegion_(IR) {}
//
//    // Get the clobbered use operand.  NULL implies no clobber possible.
//    MachineOperand *getOperand() const { return Operand_; };
//
//    // Get the region after which Operand is clobbered.  NULL implies multiple
//    // such regions or the regions are unknown.
//    IdempotentRegion *getDominatingRegion() const { return DominatingRegion_; };
//
//   private:
//    MachineOperand *Operand_;
//    IdempotentRegion *DominatingRegion_;
//  };
//}
//
//static MachineBasicBlock *getPHIIncomingMBB(const MachineInstr &MI,
//                                            MachineOperand *MO) {
//  assert(MI.isPHI());
//  for (unsigned I = 1, E = MI.getNumOperands(); I != E; I += 2)
//    if (&MI.getOperand(I) == MO)
//      return MI.getOperand(I + 1).getMBB();
//  llvm_unreachable("Incoming MBB not found");
//}
//
//static void dumpClobbers(const MachineOperand &MO,
//                         const SmallVectorImpl<UseToRewrite> &Uses,
//                         const TargetRegisterInfo *TRI) {
//
//  const MachineInstr *DefMI = MO.getParent();
//  dbgs() << " Def of " << PrintReg(MO.getReg(), TRI) << " in BB#" 
//    << DefMI->getParent()->getNumber() << ": " << *DefMI;
//  for (unsigned I = 0; I < Uses.size(); ++I) {
//    MachineOperand *UseMO = Uses[I].getOperand();
//    MachineInstr *UseMI = UseMO->getParent();
//    MachineBasicBlock *UseMBB = UseMI->getParent();
//    if (UseMI->isPHI())
//      UseMBB = getPHIIncomingMBB(*UseMI, UseMO);
//    IdempotentRegion *R = Uses[I].getDominatingRegion();
//    dbgs() << "  Potentially clobbers use";
//    if (R)
//      dbgs() << " dominated by region " << *R; 
//    dbgs() << " in BB#" << UseMBB->getNumber() << ": " << *UseMI;
//  }
//}
//
//void PatchMachineIdempotentRegions::processBackEdgeDef(
//    const Regions &BackEdgeRegions,
//    MachineOperand *Def,
//    bool *Repeat) {
//
//  unsigned DefVReg = Def->getReg();
//  MachineInstr *DefMI = Def->getParent();
//  MachineBasicBlock *DefMBB = DefMI->getParent();
//
//  // Find all potentially clobbered uses.
//  SmallVector<UseToRewrite, 4> UsesToRewrite;
//  for (MachineRegisterInfo::use_nodbg_iterator
//        U = MRI_->use_nodbg_begin(DefVReg),
//        UE = MRI_->use_nodbg_end(); U != UE; ++U) {
//    UseToRewrite Use = buildUseToRewrite(BackEdgeRegions, Def, &U.getOperand());
//    if (Use.getOperand() != NULL)
//      UsesToRewrite.push_back(Use);
//  }
//
//  // Return early if we'd be wasting our time.
//  if (UsesToRewrite.empty())
//    return;
//  DEBUG(dumpClobbers(*Def, UsesToRewrite, TRI_));
//
//  // SSAUpdater is a beautiful thing.  We'll use it to update the uses we can't
//  // otherwise trivially handle.  Make DefVReg available right now and it will
//  // be overwritten by any CopyVReg that shares the same block as DefMI.
//  typedef SmallVector<MachineInstr *, 4> InsertedMIsTy;
//  InsertedMIsTy InsertedMIs;
//  MachineSSAUpdater SSAUpdater(*MF_, &InsertedMIs);
//  SSAUpdater.Initialize(DefVReg);
//  SSAUpdater.AddAvailableValue(DefMBB, DefVReg);
//
//  // Insert copies immediately before BackEdgeRegion entry points.
//  typedef DenseMap<IdempotentRegion *, unsigned> CopyVRegsTy;
//  CopyVRegsTy CopyVRegs;
//  for (Regions::const_iterator R = BackEdgeRegions.begin(),
//       RE = BackEdgeRegions.end(); R != RE; ++R) {
//    IdempotentRegion *BackEdgeRegion = *R;
//    MachineBasicBlock::iterator RegionEntry(&BackEdgeRegion->getEntry());
//
//    // This back-edge region is not involved in generating any clobber if DefMI
//    // does not dominate.
//    if (!MDT_->dominates(DefMI, RegionEntry)) {
//      DEBUG(dbgs() << "  Skipping innocent region " << *BackEdgeRegion << "\n");
//      continue;
//    }
//
//    // It still might be involved, so insert the copy.
//    unsigned CopyVReg = MRI_->createVirtualRegister(MRI_->getRegClass(DefVReg));
//    MachineInstr *CopyMI = BuildMI(*RegionEntry->getParent(),
//                                   RegionEntry,
//                                   DefMI->getDebugLoc(),
//                                   TII_->get(TargetOpcode::COPY),
//                                   CopyVReg)
//      .addReg(DefVReg, 0, Def->getSubReg());
//    DEBUG(dbgs() << "  Created copy at entry of " << *BackEdgeRegion << ": "
//          << *CopyMI);
//    InsertedMIs.push_back(CopyMI);
//
//    // If CopyMI is in DefMBB, then it will definitely use DefVReg.  Outside of
//    // DefMBB, however, CopyMI may be reachable by some other back-edge region,
//    // in which case it should use that region's copy along that path.
//    MachineBasicBlock *CopyMBB = CopyMI->getParent();
//    if (CopyMBB != DefMBB)
//      UsesToRewrite.push_back(UseToRewrite(&CopyMI->getOperand(1)));
//
//    // The copy holds an available value.
//    assert((CopyMBB == DefMBB || !SSAUpdater.HasValueForBlock(CopyMBB)) &&
//           "copy block already has non-def available value");
//    SSAUpdater.AddAvailableValue(CopyMBB, CopyVReg);
//    CopyVRegs[BackEdgeRegion] = CopyVReg;
//  }
//
//  // If no CopyVRegs then Def does not dominate any back-edge.  No problems.
//  if (CopyVRegs.empty())
//    return;
//
//  // Rewrite uses now.
//  for (SmallVectorImpl<UseToRewrite>::iterator I = UsesToRewrite.begin(),
//       IE = UsesToRewrite.end(); I != IE; ++I) {
//    UseToRewrite &Use = *I;
//    MachineOperand *UseMO = Use.getOperand();
//    MachineInstr *UseMI = UseMO->getParent();
//    DEBUG(dbgs() << "  Rewriting use " << *UseMO << " in " << *UseMI);
//
//    // A more general case that includes the block-local case that SSAUpdater
//    // won't handle we can handle efficiently.  That is the case where UseMI is
//    // contained in this loop and is dominated by a region (there can be only
//    // one such region).  In this case, there is a single dominating copy that
//    // we can use.
//    IdempotentRegion *DomRegion = Use.getDominatingRegion();
//    if (DomRegion != NULL) {
//      unsigned CopyVReg = CopyVRegs[DomRegion];
//      DEBUG(dbgs() << "   Using CopyVReg " << PrintReg(CopyVReg, TRI_) << "\n");
//      assert(CopyVReg != 0 && "no copy coming out of clobber region");
//      UseMO->setReg(CopyVReg);
//      continue;
//    }
//
//    // Use SSAUpdater for everything else.
//    SSAUpdater.RewriteUse(*UseMO);
//  }
//
//  // Clean-up unused copies and PHIs.  Iterate until we converge.
//  InsertedMIsTy::iterator I, J, E;
//  do {
//    for (I = InsertedMIs.begin(), J = I, E = InsertedMIs.end(); I != E; ++I) {
//      MachineInstr *MI = *I;
//
//      // Erase and skip over if MI has no uses or its only user is itself.
//      unsigned VReg = MI->getOperand(0).getReg();
//      if (MRI_->use_nodbg_empty(VReg) || 
//          (MRI_->hasOneNonDBGUse(VReg) && &*MRI_->use_begin(VReg).getParent() == MI)) {
//        DEBUG(dbgs() << "  Erasing useless MI " << *MI);
//        MI->eraseFromParent();
//      } else {
//        *(J++) = MI;
//      }
//    }
//
//    // Adjust size and repeat if we erased any instructions.
//    if (I != J)
//      InsertedMIs.resize(J - InsertedMIs.begin());
//  } while (I != J);
//
//  // Any copies inserted in this loop will not affect other loops.  The same
//  // applies for PHIs inserted in this loop.  However, the defs and uses of PHIs
//  // outside of this loop may have created a back-edge clobber along some other
//  // loop back-edge and we need to check all such loops again. 
//  //
//  // Don't bother trying to isolate exactly which loops were affected -- it's
//  // very complicated -- just take another pass over everything when we're done.
//  for (I = InsertedMIs.begin(), E = InsertedMIs.end(); I != E && !*Repeat; ++I)
//    if ((*I)->isPHI() && !isCurLoopBlock(*(*I)->getParent()))
//      *Repeat = true;
//}
//
//UseToRewrite PatchMachineIdempotentRegions::buildUseToRewrite(
//    const Regions &BackEdgeRegions,
//    MachineOperand *DefMO,
//    MachineOperand *UseMO) const {
//
//  MachineInstr *UseMI = UseMO->getParent(), *DefMI = DefMO->getParent();
//  MachineBasicBlock *UseMBB = UseMI->getParent(), *DefMBB = DefMI->getParent();
//
//  // PHIs are logically used in the incoming block corresponding with UseMO.
//  if (UseMI->isPHI())
//    UseMBB = getPHIIncomingMBB(*UseMI, UseMO);
//
//  // If the use is outside the loop, the use may need to be rewritten if any
//  // back-edge reaches an exit.  Assume one does.
//  if (!isCurLoopBlock(*UseMBB))
//    return UseToRewrite(UseMO);
//
//  // Inside this loop, a use that is dominated by a back-edge region will
//  // definitely be clobbered by a back-edge definition inside that region.
//  // Using the copy placed immediately before the dominating back-edge boundary
//  // will remove the clobber.
//  IdempotentRegion *R = getDominatingRegion(*UseMI, *UseMBB, BackEdgeRegions);
//  if (R)
//    return UseToRewrite(UseMO, R);
//
//  // If the use basic block is the same as the def block and there was no single
//  // dominating back-edge region (we just checked that immediately above) then
//  // the use cannot be clobbered along any back-edge.  No clobber.
//  if (UseMBB == DefMBB)
//    return UseToRewrite();
//
//  // Other uses may be clobbered if the use is reachable through some
//  // non-dominating back-edge region.  Assume it is.
//  return UseToRewrite(UseMO);
//}
//
//IdempotentRegion *PatchMachineIdempotentRegions::getDominatingRegion(
//    const MachineInstr &UseMI, 
//    const MachineBasicBlock &UseMBB, 
//    const Regions &BackEdgeRegions) const {
//
//  // Get the region that dominates UseMI.  There can be no more than one.
//  IdempotentRegion *DominatingRegion = NULL;
//  for (Regions::const_iterator R = BackEdgeRegions.begin(),
//       RE = BackEdgeRegions.end(); R != RE; ++R) {
//    IdempotentRegion *Region = *R;
//    if (UseMI.isPHI()) {
//      if (!MDT_->dominates(&Region->getEntryMBB(), &UseMBB))
//        continue;
//    } else {
//      if (!MDT_->dominates(&Region->getEntry(), &UseMI))
//        continue;
//    }
//    assert(DominatingRegion == NULL && "multiple dominating regions");
//    DominatingRegion = Region;
//  }
//  return DominatingRegion;
//}
//
//void PatchMachineIdempotentRegions::clearCurLoopInfo() {
//  CurLoop_ = NULL;
//  CurLoopBlocks_.clear();
//  CurLoopExits_.clear();
//  CurLoopExiting_.clear();
//}
//
//void PatchMachineIdempotentRegions::setCurLoopInfo(MachineLoop *Loop) {
//  clearCurLoopInfo();
//
//  // CurLoop_.
//  CurLoop_ = Loop;
//
//  // CurLoopBlocks_.  Sorted for fast queries.
//  CurLoopBlocks_.resize(Loop->getNumBlocks());
//  std::copy(Loop->block_begin(), Loop->block_end(), CurLoopBlocks_.begin());
//  array_pod_sort(CurLoopBlocks_.begin(), CurLoopBlocks_.end());
//
//  // CurLoopExits_.  Sorted for fast queries.
//  Loop->getExitBlocks(CurLoopExits_);
//  array_pod_sort(CurLoopExits_.begin(), CurLoopExits_.end());
//
//  // CurLoopExiting_.
//  Loop->getExitingBlocks(CurLoopExiting_);
//}
//
//bool PatchMachineIdempotentRegions::isCurLoopHeader(
//    const MachineBasicBlock &MBB) const {
//  return CurLoop_->getHeader() == &MBB;
//}
//
//bool PatchMachineIdempotentRegions::isCurLoopBlock(
//    const MachineBasicBlock &MBB) const {
//  return std::binary_search(CurLoopBlocks_.begin(), CurLoopBlocks_.end(),
//                            const_cast<MachineBasicBlock *>(&MBB));
//}
//
//bool PatchMachineIdempotentRegions::isCurLoopExit(
//    const MachineBasicBlock &MBB) const {
//  return std::binary_search(CurLoopExits_.begin(), CurLoopExits_.end(),
//                            const_cast<MachineBasicBlock *>(&MBB));
//}
//

