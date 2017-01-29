//===-------- MachineIdempotentRegions.h ------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the interface for querying and updating the idempotent
// region information at the machine level.  A "machine" idempotent region is
// defined by the single IDEM instruction that defines its entry point and it
// spans all instructions reachable by control flow from the entry point to
// subsequent IDEM instructions.
//
// The IdempotentRegion class provides both an instruction-level iterator
// (IdempotentRegion::inst_iterator) and a block-level iterator
// (IdempotentRegion::mbb_iterator) for scanning a region in depth-first order
// from the entry point.  This is a fairly common task, employed in a variety of
// analyses.  Most of the source code in this file is in defining these
// templated iterators.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEIDEMPOTENTREGIONS_H
#define LLVM_CODEGEN_MACHINEIDEMPOTENTREGIONS_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/SparseBitVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/IdempotenceOptions.h"
#include "llvm/CodeGen/LiveInterval.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/SlotIndexes.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Target/TargetInstrInfo.h"
#include <iterator>

namespace llvm {

//===----------------------------------------------------------------------===//
// IdempotentRegion
//===----------------------------------------------------------------------===//

// Type mappings used by IdempotentRegion iterators.
template <typename> struct MITraits {};
template <> struct MITraits<MachineInstr> {
  typedef MachineBasicBlock ParentTy;
};
template <> struct MITraits<const MachineInstr> {
  typedef const MachineBasicBlock ParentTy;
};

class IdempotentRegion;
raw_ostream &operator<<(raw_ostream &OS, const IdempotentRegion &R);

class IdempotentRegion {
 public:
  // Return the entry instruction for this region.
  MachineInstr &getEntry() const { return *Entry_; }

  // Return the entry basic block for this region.
  MachineBasicBlock &getEntryMBB() const { return *(Entry_->getParent()); }

  // dfs_mbb_iterator - Iterator for forward iterating over basic blocks in a
  // region as a [start, end] instruction pair (not including exit boundary
  // instructions).  Heavy-weight; do not pass by value.  Try to re-use as much
  // as possible.
  template <typename MachineInstrTy>
  class dfs_mbb_iterator
      : public std::iterator<
            std::forward_iterator_tag, 
            std::pair<MachineInstrTy *, MachineInstrTy *> > {
  public:
    // Typedefs.
    typedef typename MITraits<MachineInstrTy>::ParentTy MachineBasicBlockTy;
    typedef MachineBasicBlock::bundle_iterator<
      MachineInstrTy, ilist_iterator<MachineInstrTy> > MachineBasicBlockItTy;
    typedef SmallPtrSet<MachineBasicBlockTy *, 32> VisitedTy;

    // Constructors.
    explicit dfs_mbb_iterator(const IdempotentRegion &Region)
        : Region_(&Region), TII_(NULL), Valid_(false), Skip_(false) {
      MachineBasicBlockItTy I = &Region.getEntry();
      MachineBasicBlockTy *MBB = I->getParent();
      init(MBB, I);
    }
    dfs_mbb_iterator(const IdempotentRegion &Region, MachineBasicBlockTy *MBB)
        : Region_(&Region), TII_(NULL), Valid_(false), Skip_(false) {
      if (MBB)
        init(MBB, MBB->begin());
    }
    dfs_mbb_iterator(const IdempotentRegion &Region,
                     MachineBasicBlockTy *MBB,
                     MachineBasicBlockItTy I)
        : Region_(&Region), TII_(NULL), Valid_(false), Skip_(false) {
      init(MBB, I);
    }

    // Return whether the iterator is valid.  False implies the end condition
    // has been met.
    bool isValid() { return Valid_; }

    // Return whether this MBB range contains an exit instruction or if MBB
    // exits the function (and hence exits the region at its end).
    bool isExiting() const {
      if (End_ != MBB_->end() || MBB_->succ_empty())
        return true;
      return false;
    }

    // Skip the depth-first search along the current path.
    void skip() { Skip_ = true; }

    // Get the set of blocks visited so far along the depth-first search.
    VisitedTy &getVisitedSet() { return Visited_; }

    // Get the MBB currently assigned to this iterator.
    MachineBasicBlockTy &getMBB() const { return *MBB_; }

    // Comparison.
    bool operator==(const dfs_mbb_iterator &X) const;
    bool operator!=(const dfs_mbb_iterator &X) const { return !operator==(X); }

    // Pre-increment and post-increment.
    dfs_mbb_iterator<MachineInstrTy> &operator++();
    dfs_mbb_iterator<MachineInstrTy> operator++(int) {
      dfs_mbb_iterator Tmp = *this; ++*this; return Tmp;
    }

    // Dereference.
    std::pair<MachineBasicBlockItTy, MachineBasicBlockItTy> operator*() const {
      return std::make_pair(Start_, End_);
    }

    // Alternative accessor.
    std::pair<SlotIndex, SlotIndex> getSlotRange(const SlotIndexes &SLI) const {
      SlotIndex StartSlot = (Start_ != Init_ || End_ == Init_) ?
        SLI.getMBBStartIdx(MBB_) :
        SLI.getInstructionIndex(Start_).getRegSlot();
      SlotIndex EndSlot = (End_ == MBB_->end()) ?
        SLI.getMBBEndIdx(MBB_) :
        SLI.getInstructionIndex(End_).getRegSlot();
      return std::make_pair(StartSlot, EndSlot);
    }

    // Debugging.
    void print(raw_ostream &OS) const {
      OS << "mbb_iterator for " << *Region_ << " in BB#" << MBB_->getNumber()
        << ", Valid? " << Valid_ << "\n";
    }

  private:
    const IdempotentRegion *Region_;
    const TargetInstrInfo *TII_;
    bool Valid_;
    bool Skip_;
    MachineBasicBlockItTy Init_;
    MachineBasicBlockItTy Start_;
    MachineBasicBlockItTy End_;
    MachineBasicBlockTy *MBB_;
    VisitedTy Visited_; 
    SmallVector<MachineBasicBlockTy *, 16> Worklist_;

    // Set the iterator position.  Assumes I is contained inside Region_.
    void init(MachineBasicBlockTy *MBB, MachineBasicBlockItTy I);

    // Return whether MI exits the region.  Calls are treated as exits.
    bool isExit(MachineInstrTy *MI) const {
      return ((TII_->isIdemBoundary(MI) && MI != &Region_->getEntry()) ||
              MI->isCall());
    }

    // Return the value that should be cached in End_.
    MachineBasicBlockItTy computeEnd() const;
  };

  typedef dfs_mbb_iterator<MachineInstr> mbb_iterator;
  typedef dfs_mbb_iterator<const MachineInstr> const_mbb_iterator;

  mbb_iterator       mbb_begin()       { return mbb_iterator(*this); }
  mbb_iterator       mbb_end()         { return mbb_iterator(*this, NULL); }
  const_mbb_iterator mbb_begin() const { return const_mbb_iterator(*this); }
  const_mbb_iterator mbb_end()   const {
    return const_mbb_iterator(*this, NULL);
  }

  // dfs_inst_iterator - Iterator for forward iterating over instructions in a
  // region (not including exit boundary instructions).  Heavy-weight; do not
  // pass by value.  Try to re-use as much as possible.
  template <typename MachineInstrTy>
  class dfs_inst_iterator
      : public std::iterator<std::forward_iterator_tag, MachineInstrTy *> {
  public:
    // Constructors.
    explicit dfs_inst_iterator(const IdempotentRegion &Region)
        : MBBIterator_(Region) {
      tie(It_, End_) = *MBBIterator_;
    };
    dfs_inst_iterator(const IdempotentRegion &Region, MachineInstrTy *MI)
        : MBBIterator_(Region, MI->getParent(), MI) {
      tie(It_, End_) = *MBBIterator_;
    }

    // Return whether the iterator is valid.  False implies the end condition
    // has been met.
    bool isValid() { return MBBIterator_.isValid(); }

    // Comparison.
    bool operator==(const dfs_inst_iterator &X) const {
      return MBBIterator_ == X.MBBIterator_ && It_ == X.It__;
    }
    bool operator!=(const dfs_inst_iterator &X) const {
      return !operator==(X);
    }

    // Pre-increment and post-increment.
    dfs_inst_iterator<MachineInstrTy> &operator++();
    dfs_inst_iterator<MachineInstrTy> operator++(int) {
      dfs_inst_iterator Tmp = *this; ++*this; return Tmp;
    }

    // Dereference.
    MachineInstrTy *operator*() const { return It_; }

    // Debugging.
    void print(raw_ostream &OS) const {
      OS << "inst_iterator:\n" << *It_ << "in "; MBBIterator_.print(OS);
    }

  private:
    dfs_mbb_iterator<MachineInstrTy> MBBIterator_;
    typename dfs_mbb_iterator<MachineInstrTy>::MachineBasicBlockItTy It_;
    typename dfs_mbb_iterator<MachineInstrTy>::MachineBasicBlockItTy End_;
  };

  typedef dfs_inst_iterator<MachineInstr> inst_iterator;
  typedef dfs_inst_iterator<const MachineInstr> const_inst_iterator;

  inst_iterator       inst_begin()       { return inst_iterator(*this); }
  inst_iterator       inst_end()         { return inst_iterator(*this, NULL); }
  const_inst_iterator inst_begin() const { return const_inst_iterator(*this); }
  const_inst_iterator inst_end()   const {
    return const_inst_iterator(*this, NULL);
  }

  // Debugging.
  void dump() const;
  void print(raw_ostream &OS, const SlotIndexes *SI = 0) const;

 private:
  friend class MachineIdempotentRegions;

  // Constructor.
  IdempotentRegion(unsigned ID, MachineInstr *Entry)
    : ID_(ID),
      Entry_(Entry),
      TII_(Entry->getParent()->getParent()->getSubtarget().getInstrInfo()) {}

  // A unique identifier.
  unsigned ID_;

  // The region entry instruction.
  MachineBasicBlock::iterator Entry_;

  // Need TII to check for boundaries.
  const TargetInstrInfo *TII_;

  // Do not implement.
  IdempotentRegion();
};

//===----------------------------------------------------------------------===//
// IdempotentRegion templated method definitions
//===----------------------------------------------------------------------===//

template <typename MachineInstrTy>
void IdempotentRegion::dfs_mbb_iterator<MachineInstrTy>::init(
    MachineBasicBlockTy *MBB,
    MachineBasicBlockItTy I) {
  assert(MBB && "MBB not set");
  //assert(MBB->getParent()->getRegInfo().isPatched() &&
  //       "calls and loop dominating regions are not yet patched");

  // Starting at an idempotent boundary that is not the entry we immediately
  // satisfy the end condition.
  TII_ = MBB->getParent()->getSubtarget().getInstrInfo();
  MachineBasicBlockItTy Entry = &Region_->getEntry();
  Valid_ = (!TII_->isIdemBoundary(I) || I == Entry);
  if (!Valid_)
    return;

  // OK.
  Visited_.clear();
  Worklist_.clear();
  MBB_ = MBB;
  Init_ = Start_ = End_ = I;
  while (End_ != MBB_->end() && !isExit(End_))
    ++End_;
}

template <typename MachineInstrTy>
typename
IdempotentRegion::dfs_mbb_iterator<MachineInstrTy>::MachineBasicBlockItTy 
IdempotentRegion::dfs_mbb_iterator<MachineInstrTy>::computeEnd() const {
  MachineBasicBlockItTy I = Start_, IE = MBB_->end();
  MachineBasicBlockItTy Entry = &Region_->getEntry();
  for (; I != IE; ++I) {
    assert(I != Entry && "wrap to entry should be impossible after patching");
    if (I == Init_ || isExit(I))
      return I;
  }
  return IE;
}

template <typename MachineInstrTy>
bool IdempotentRegion::dfs_mbb_iterator<MachineInstrTy>::operator==(
    const dfs_mbb_iterator &X) const {
  assert(Region_ == X.Region_ &&
         "cannot compare iterators from different regions");
  if (!Valid_)
    return !X.Valid_;
  return Start_ == X.Start_ && End_ == X.End_ && Valid_ == X.Valid_;
}

template <typename MachineInstrTy>
IdempotentRegion::dfs_mbb_iterator<MachineInstrTy> &
IdempotentRegion::dfs_mbb_iterator<MachineInstrTy>::operator++() {
  assert(Valid_ && "iterating past end condition");

  // If MBB_ does not exit, add successors to the worklist. 
  if (!Skip_ && !isExiting())
    for (MachineBasicBlock::const_succ_iterator S = MBB_->succ_begin(),
        SE = MBB_->succ_end(); S != SE; ++S)
      if (Visited_.insert(*S).second)
        Worklist_.push_back(*S);
  Skip_ = false;

  // Pop off the next block on the work list, if any.
  if (Worklist_.empty()) {
    Valid_ = false;
    return *this;
  }
  MBB_ = Worklist_.pop_back_val();
  Start_ = MBB_->begin();
  End_ = computeEnd();
  return *this;
}

template <typename MachineInstrTy>
IdempotentRegion::dfs_inst_iterator<MachineInstrTy> &
IdempotentRegion::dfs_inst_iterator<MachineInstrTy>::operator++() {
  assert(MBBIterator_.isValid() && "iterating past end condition");
  if (++It_ == End_) {
    do {
      if (!(++MBBIterator_).isValid())
        return *this;
      tie(It_, End_) = *MBBIterator_;
    } while (It_ == End_);
  }
  return *this;
}

//===----------------------------------------------------------------------===//
// MachineIdempotentRegions
//===----------------------------------------------------------------------===//

class MachineIdempotentRegions : public MachineFunctionPass {
 public:
  static char ID;
  MachineIdempotentRegions() : MachineFunctionPass(ID) {
    initializeMachineIdempotentRegionsPass(*PassRegistry::getPassRegistry());
  }

  virtual void getAnalysisUsage(AnalysisUsage &AU) const;
  virtual void print(raw_ostream &O, const Module* = 0) const;
  virtual void releaseMemory();
  virtual bool runOnMachineFunction(MachineFunction &MF);

  // Region iterators.  The region returned by begin() is always the region that
  // starts at the entry point of the function.
  typedef SmallVectorImpl<IdempotentRegion *>::iterator       iterator;
  typedef SmallVectorImpl<IdempotentRegion *>::const_iterator const_iterator;

  iterator       begin()       { return Regions_.begin(); }
  const_iterator begin() const { return Regions_.begin(); }
  iterator       end()         { return Regions_.end();   }
  const_iterator end()   const { return Regions_.end();   }
  bool           empty() const { return Regions_.empty(); }
  unsigned       size()  const { return Regions_.size();  }

  // Insert a region boundary before MI and update the analysis as necessary.
  IdempotentRegion &createRegionBefore(MachineBasicBlock *MBB,
                                       MachineBasicBlock::iterator MI,
                                       SlotIndexes *Indexes = NULL);

  // Return whether MI starts a region.  All regions start at a boundary.
  bool isRegionEntry(const MachineInstr &MI) const {
    return TII_->isIdemBoundary(&MI);
  }

  // Return the region that starts at MI.
  IdempotentRegion &getRegionAtEntry(const MachineInstr &MI) {
    EntryToRegionMap::iterator It = EntryToRegionMap_.find(&MI);
    assert(It != EntryToRegionMap_.end());
    return *It->second;
  }

  // Return in Regions the set of regions that contain MI.  If MI is a region
  // boundary then the function will return the set of regions that precede MI's
  // region.
  void getRegionsContaining(const MachineInstr &MI,
                            SmallVectorImpl<IdempotentRegion *> *Regions);

  // Return whether LI is live across one or more region boundaries.
  bool isLiveAcrossRegions(const LiveInterval &LI,
                           const SlotIndexes &SLI) const {
    for (const_iterator R = begin(), RE = end(); R != RE; ++R) 
      if (LI.liveAt(SLI.getInstructionIndex(&(*R)->getEntry()).getRegSlot()))
        return true;
    return false;
  }

  // Verify that MI does not overwrite any registers in the set LiveIns.
  bool verifyInstruction(const MachineInstr &MI,
                         const DenseSet<unsigned> &LiveIns,
                         const SlotIndexes *Indexes) const;

private:
  MachineFunction *MF_;
  const TargetInstrInfo    *TII_;
  const TargetRegisterInfo *TRI_;
  MachineDominatorTree *DT_; 

  // Region allocation and storage.
  BumpPtrAllocator RegionAllocator_;
  SmallVector<IdempotentRegion *, 16> Regions_;

  // Map of region entry instructions to regions.
  typedef DenseMap<const MachineInstr *, IdempotentRegion *> EntryToRegionMap;
  EntryToRegionMap EntryToRegionMap_;

  // Create a region at the boundary instruction MI.
  IdempotentRegion &createRegionAtBoundary(MachineInstr *MI);


  void killDummyCalls(MachineFunction &MF);
  void wrapCalls(MachineFunction &MF);
  void fixStackSpills(MachineFunction &MF);
  bool searchForPriorBoundaries(MachineBasicBlock::iterator I);
  void removeDuplicates(MachineFunction &MF);
  void lowerIdemToCheckpoint(MachineFunction &MF);
  void findAntidependencePairs(MachineInstr *MI);
  bool scanForAliasingLoad(MachineInstr *Store,
                           MachineBasicBlock::iterator I,
                           MachineBasicBlock::iterator E,
                           int FI);
  bool scanForAliasingLoad(MachineInstr *Store,
                           MachineBasicBlock::iterator I,
                           MachineBasicBlock::iterator E);

  // Verification helper.
  bool verifyOperand(const MachineOperand &MO,
                     const DenseSet<unsigned> &LiveIns,
                     const SlotIndexes *Indexes) const;
};

} // namespace llvm

#endif


