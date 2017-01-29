//===-------- MemoryIdempotenceAnalysis.cpp ---------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the implementation for computing the idempotent region
// information at the LLVM IR level in terms of the "cuts" that define them.
// See "Static Analysis and Compiler Design for Idempotent Processing" in PLDI
// '12.
//
// Potential cut points are captured by the CandidateInfo class, which contains
// some meta-info used in the hitting set computation.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "memory-idempotence-analysis"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/CaptureTracking.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/CodeGen/MemoryIdempotenceAnalysis.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/IR/PredIteratorCache.h"
#include <algorithm>
#include <sstream>
#include <vector>

using namespace llvm;

//===----------------------------------------------------------------------===//
// Helpers
//===----------------------------------------------------------------------===//

static bool isSubloopPreheader(const BasicBlock &BB,
                               const LoopInfo &LI) {
  Loop *L = LI.getLoopFor(&BB);
  if (L)
    for (Loop::iterator I = L->begin(), E = L->end(); I != E; ++I)
      if (&BB == (*I)->getLoopPreheader())
        return true;
  return false;
}

static std::string getLocator(const Instruction &I) {
  unsigned Offset = 0;
  const BasicBlock *BB = I.getParent();
  for (BasicBlock::const_iterator It = I; It != BB->begin(); --It)
    ++Offset;

  std::stringstream SS;
  SS << BB->getName().str() << ":" << Offset;
  return SS.str();
}

namespace {
  typedef std::pair<Instruction *, Instruction *> AntidependencePairTy;
  typedef SmallVector<Instruction *, 16> AntidependencePathTy;
}

namespace llvm {
  static raw_ostream &operator<<(raw_ostream &OS, const AntidependencePairTy &P);
  static raw_ostream &operator<<(raw_ostream &OS, const AntidependencePathTy &P);
}

raw_ostream &llvm::operator<<(raw_ostream &OS, const AntidependencePairTy &P) {
  OS << "Antidependence Pair (" << getLocator(*P.first) << ", " 
    << getLocator(*P.second) << ")";
  return OS;
}

raw_ostream &llvm::operator<<(raw_ostream &OS, const AntidependencePathTy &P) {
  OS << "[";
  for (AntidependencePathTy::const_iterator I = P.begin(), First = I,
       E = P.end(); I != E; ++I) {
    if (I != First)
      OS << ", ";
    OS << getLocator(**I);
  }
  OS << "]";
  return OS;
}

//===----------------------------------------------------------------------===//
// CandidateInfo
//===----------------------------------------------------------------------===//

namespace {
  class CandidateInfo {
   public:
    typedef SmallPtrSet<const AntidependencePathTy *, 4> UnintersectedPaths;

    // Constructor.
    CandidateInfo(Instruction *Candidate,
                  unsigned LoopDepth,
                  bool IsSubloopPreheader);

    // Get the candidate instruction.
    Instruction *getCandidate() { return Candidate_; }
    const Instruction *getCandidate() const { return Candidate_; }

    // Iteration support (const only).
    typedef UnintersectedPaths::const_iterator const_iterator;
    const_iterator begin()  const { return UnintersectedPaths_.begin(); }
    const_iterator end()    const { return UnintersectedPaths_.end(); }
    unsigned       size()   const { return UnintersectedPaths_.size(); }
    bool           empty()  const { return UnintersectedPaths_.empty(); }

    // Add Path to the set of unintersected paths and update priority.
    void add(const AntidependencePathTy &Path);

    // Remove Path from the set of unintersected paths and update priority.
    void remove(const AntidependencePathTy &Path);

    // Debugging support.
    void print(raw_ostream &OS) const;

    // Priority comparison function.
    static bool compare(CandidateInfo *L, CandidateInfo *R) {
      return (L->Priority_ < R->Priority_);
    }

   private:
    Instruction *Candidate_;
    UnintersectedPaths UnintersectedPaths_;

    union {
      // Higher priority is better.
      struct {
        // From least important to most important (little endian):
        signed IntersectedPaths:16;    // prefer more already-intersected paths
        signed IsSubloopPreheader:8;   // prefer preheaders
        signed IsAntidependentStore:8; // prefer antidependent stores
        signed UnintersectedPaths:16;  // prefer more unintersected paths
        signed LoopDepth:16;           // (inverted) prefer outer loops
      } PriorityElements_;
      uint64_t Priority_;
    };

    // Do not implement.
    CandidateInfo();
  };

  typedef std::vector<CandidateInfo *> WorklistTy; 
} // end anonymous namespace

CandidateInfo::CandidateInfo(Instruction *Candidate,
                             unsigned LoopDepth,
                             bool IsSubloopPreheader)
    : Candidate_(Candidate), Priority_(0) {
  PriorityElements_.LoopDepth = ~LoopDepth;
  PriorityElements_.IsAntidependentStore = false;
  PriorityElements_.IsSubloopPreheader = IsSubloopPreheader;
  PriorityElements_.UnintersectedPaths = 0;
  PriorityElements_.IntersectedPaths = 0;
}

void CandidateInfo::print(raw_ostream &OS) const {
  OS << "Candidate " << getLocator(*Candidate_)
    << "\n Priority:              " << Priority_
    << "\n  LoopDepth:            " << PriorityElements_.LoopDepth
    << "\n  UnintersectedPaths:   " << PriorityElements_.UnintersectedPaths
    << "\n  IsAntidependentStore: " << PriorityElements_.IsAntidependentStore
    << "\n  IsSubloopPreheader:   " << PriorityElements_.IsSubloopPreheader
    << "\n  IntersectedPaths:     " << PriorityElements_.IntersectedPaths
    << "\n";
}

void CandidateInfo::add(const AntidependencePathTy &Path) {
  // Antidependent stores are always the first store on the path.
  if (Candidate_ == *Path.begin())
    PriorityElements_.IsAntidependentStore = true;

  // Update other structures.
  PriorityElements_.UnintersectedPaths++;
  assert(UnintersectedPaths_.insert(&Path).second && "already inserted");
}

void CandidateInfo::remove(const AntidependencePathTy &Path) {
  // Update priority.
  PriorityElements_.UnintersectedPaths--;
  PriorityElements_.IntersectedPaths++;
  assert(PriorityElements_.UnintersectedPaths >= 0 &&
         PriorityElements_.IntersectedPaths >= 0 && "Wrap around");

  // Remove Path from the list of unintersected paths.
  assert(UnintersectedPaths_.erase(&Path) && "path not in set");
  assert(static_cast<unsigned>(PriorityElements_.UnintersectedPaths) ==
         UnintersectedPaths_.size());
}

//===----------------------------------------------------------------------===//
// MemoryIdempotenceAnalysisImpl
//===----------------------------------------------------------------------===//

class llvm::MemoryIdempotenceAnalysisImpl {
 private:
  // Constructor.
  MemoryIdempotenceAnalysisImpl(MemoryIdempotenceAnalysis *MIA) : MIA_(MIA) {}

  // Forwarded function implementations.
  void releaseMemory();
  void print(raw_ostream &OS, const Module *M = 0) const;
  bool runOnFunction(Function &F);

 private:
  friend class MemoryIdempotenceAnalysis;
  MemoryIdempotenceAnalysis *MIA_;

  // Final output structure.
  MemoryIdempotenceAnalysis::CutSet CutSet_;
  MemoryIdempotenceAnalysis::AntidependenceCutMapTy CutMap_;

  // Intermediary data structure 1.
  typedef SmallVector<AntidependencePairTy, 16> AntidependencePairs;
  AntidependencePairs AntidependencePairs_;

  // Intermediary data structure 2.
  typedef SmallVector<AntidependencePathTy, 16> AntidependencePaths;
  AntidependencePaths AntidependencePaths_;

  // Other things we use.
  PredIteratorCache PredCache_;
  Function *F_;
  AliasAnalysis *AA_;
  DominatorTree *DT_;
  LoopInfo *LI_;

  // Helper functions.
  void forceCut(BasicBlock::iterator I);
  void findAntidependencePairs(Instruction *Write);
  bool scanForAliasingLoad(BasicBlock::iterator I,
                           BasicBlock::iterator E,
                           StoreInst *Store);
  void computeAntidependencePaths();
  void computeHittingSet();
  void processRedundantCandidate(CandidateInfo *RedundantInfo,
                                 WorklistTy *Worklist,
                                 const AntidependencePathTy &Path);
};

void MemoryIdempotenceAnalysisImpl::releaseMemory() {
  CutSet_.clear();
  CutMap_.clear();
  AntidependencePairs_.clear();
  AntidependencePaths_.clear();
  PredCache_.clear();
}

static bool forcesCut(const Instruction &I) {
  // See comment at the head of forceCut() further below.
  if (const LoadInst *L = dyn_cast<LoadInst>(&I))
    return L->isVolatile();
  if (const StoreInst *S = dyn_cast<StoreInst>(&I))
    return S->isVolatile();
  if (const CallInst *CI = dyn_cast<CallInst>(&I))
    return !(CI->isTailCall());
  return (isa<InvokeInst>(I) ||
          isa<VAArgInst>(&I) ||
          isa<FenceInst>(&I) ||
          isa<AtomicCmpXchgInst>(&I) ||
          isa<AtomicRMWInst>(&I));
}

bool MemoryIdempotenceAnalysisImpl::runOnFunction(Function &F) {
  F_  = &F;
  AA_ = &MIA_->getAnalysis<AliasAnalysis>();
  DT_ = &MIA_->getAnalysis<DominatorTreeWrapperPass>().getDomTree();
  LI_ = &MIA_->getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
  DEBUG(dbgs() << "\n*** MemoryIdempotenceAnalysis for Function "
        << F_->getName() << " ***\n");

  DEBUG(dbgs() << "\n** Computing Forced Cuts\n");
  for (Function::iterator BB = F.begin(); BB != F.end(); ++BB)
    for (BasicBlock::iterator I = BB->begin(); I != BB->end(); ++I)
      if (forcesCut(*I))
        forceCut(I);

  DEBUG(dbgs() << "\n** Computing Memory Antidependence Pairs\n");
  for (Function::iterator BB = F.begin(); BB != F.end(); ++BB)
    for (BasicBlock::iterator I = BB->begin(); I != BB->end(); ++I)
      //if (isa<StoreInst>(I) || isa<CallInst>(I) || isa<MemIntrinsic>(I))
      if (isa<StoreInst>(I))
        findAntidependencePairs(I);

  // Return early if there's nothing to analyze.
  if (AntidependencePairs_.empty())
    return false;

  DEBUG(dbgs() << "\n** Computing Paths to Cut\n");
  computeAntidependencePaths();

  DEBUG(dbgs() << "\n** Computing Hitting Set\n");
  computeHittingSet();

  DEBUG(print(dbgs()));
  return false;
}

void MemoryIdempotenceAnalysisImpl::forceCut(BasicBlock::iterator I) {
  // These cuts actually need to occur at the machine level.  Calls and invokes
  // are one common case that we are handled after instruction selection; see
  // patchCallingConvention() in PatchMachineIdempotentRegions.  In the absence
  // of any actual hardware support, the others are just approximated here.
  if (CallSite(I))
    return;

  DEBUG(dbgs() << " Inserting forced cut at " << getLocator(*I) << "\n");
  CutSet_.insert(++I);
}

void MemoryIdempotenceAnalysisImpl::findAntidependencePairs(Instruction *Write) {
  DEBUG(dbgs() << " Analyzing store " << getLocator(*Write) << "\n");

  // Perform a reverse depth-first search to find aliasing loads.
  typedef std::pair<BasicBlock *, BasicBlock::iterator> WorkItem;
  SmallVector<WorkItem, 8> Worklist;
  SmallPtrSet<BasicBlock *, 32> Visited;

  BasicBlock *WriteBB = Write->getParent();
  Worklist.push_back(WorkItem(WriteBB, Write));
  do {
    BasicBlock *BB;
    BasicBlock::iterator I, E;
    tie(BB, I) = Worklist.pop_back_val();

    // If we are revisiting WriteBB, we scan to Write to complete the cycle.
    // Otherwise we end at BB->begin().
    E = (BB == WriteBB && I == BB->end()) ? (BasicBlock::iterator) Write : BB->begin();

    // Scan for an aliasing load.  Terminate this path if we see one or a cut is
    // already forced.
    if(StoreInst *Store = dyn_cast<StoreInst>(Write))
    {
      if (scanForAliasingLoad(I, E, Store))
        continue;
    }

    // If the path didn't terminate, continue on to predecessors.
    for (BasicBlock **P = PredCache_.GetPreds(BB); *P; ++P)
      if (Visited.insert(*P).second)
        Worklist.push_back(WorkItem((*P), (*P)->end()));

  } while (!Worklist.empty());
}

bool MemoryIdempotenceAnalysisImpl::scanForAliasingLoad(BasicBlock::iterator I,
                                                        BasicBlock::iterator E,
                                                        StoreInst *Store) {

  Value *Pointer = Store->getOperand(1);
  unsigned PointerSize = AA_->getTypeStoreSize(Store->getOperand(0)->getType());

  while (I != E) {
    --I;
    // If we see a forced cut, the path is already cut; don't scan any further.
    if (forcesCut(*I))
      return true;

    // Otherwise, check for an aliasing load.
    if (isa<LoadInst>(I)) {
      if (AA_->getModRefInfo(I, Pointer, PointerSize) & AliasAnalysis::Ref) {
        AntidependencePairTy Pair = AntidependencePairTy(I, Store);
        DEBUG(dbgs() << "  " << Pair << "\n");
        AntidependencePairs_.push_back(Pair);
        DEBUG(dbgs() << "JVDW: Alias Pair Locations\n");
        DEBUG(dbgs() << "JVDW: Load:\t" << *AA_->getLocation(dyn_cast<LoadInst>(I)).Ptr << "\n");
        DEBUG(dbgs() << "JVDW: Store:\t" << *AA_->getLocation(Store).Ptr << "\n");
        //if (AA_->getLocation(dyn_cast<LoadInst>(I)).Ptr != AA_->getLocation(Store).Ptr)
        //  continue;
        return true;
      }
    }
  }

  // If this is the entry block to the function and it is a store to a global
  // address
  if (I->getParent() == &I->getParent()->getParent()->getEntryBlock())
    if (isa<GlobalValue>(Store->getPointerOperand()))
    {
        AntidependencePairTy Pair = AntidependencePairTy(I, Store);
        DEBUG(dbgs() << "  " << Pair << "\n");
        AntidependencePairs_.push_back(Pair);
        return true;
    }

  return false;
}

void MemoryIdempotenceAnalysisImpl::computeAntidependencePaths() {

  // Compute an antidependence path for each antidependence pair.
  for (AntidependencePairs::iterator I = AntidependencePairs_.begin(), 
       E = AntidependencePairs_.end(); I != E; ++I) {
    BasicBlock::iterator Load, Store;
    tie(Load, Store) = *I;

    // Prepare a new antidependence path.
    AntidependencePaths_.resize(AntidependencePaths_.size() + 1);
    AntidependencePathTy &Path = AntidependencePaths_.back();

    // The antidependent store is always on the path.
    Path.push_back(Store);

    // The rest of the path consists of other stores that dominate Store but do
    // not dominate Load.  Handle the block-local case quickly.
    BasicBlock::iterator Cursor = Store;
    BasicBlock *SBB = Store->getParent(), *LBB = Load->getParent();
    if (SBB == LBB && DT_->dominates(Load, Store)) {
      while (--Cursor != Load)
        if (isa<StoreInst>(Cursor))
          Path.push_back(Cursor);
      DEBUG(dbgs() << " Local " << *I << " has path " << Path << "\n");
      continue;
    }

    // Non-local case.
    BasicBlock *BB = SBB;
    DomTreeNode *DTNode = DT_->getNode(BB), *LDTNode = DT_->getNode(LBB);
    while (!DT_->dominates(DTNode, LDTNode)) {
      DEBUG(dbgs() << "  Scanning dominating block " << BB->getName() << "\n");
      BasicBlock::iterator E = BB->begin();
      while (Cursor != E)
        if (isa<StoreInst>(--Cursor))
          Path.push_back(Cursor);

      // Move the cursor to the end of BB's IDom block.
      DTNode = DTNode->getIDom();
      if (DTNode == NULL)
        break;
      BB = DTNode->getBlock();
      Cursor = BB->end();
    }
    DEBUG(dbgs() << " Non-local " << *I << " has path " << Path << "\n");
  }
}

static void dumpWorklist(const WorklistTy &Worklist) {
  dbgs() << "Worklist:\n";
  for (WorklistTy::const_iterator I = Worklist.begin(), E = Worklist.end();
       I != E; ++I)
    (*I)->print(dbgs());
  dbgs() << "\n";
}

void MemoryIdempotenceAnalysisImpl::computeHittingSet() {
  // This function does not use the linear-time version of the hitting set
  // approximation algorithm, which requires constant-time lookup and
  // constant-time insertion data structures.  This doesn't mesh well with
  // a complex priority function such as ours.  This implementation adds a
  // logarithmic factor using a sorted worklist to track priorities.  Although
  // the time complexity is slightly higher, it is much more space efficient as
  // a result.
  typedef DenseMap<const Instruction *, CandidateInfo *> CandidateInfoMapTy;
  CandidateInfoMapTy CandidateInfoMap;

  // Find all candidates and compute their priority.
  for (AntidependencePaths::iterator I = AntidependencePaths_.begin(),
       IE = AntidependencePaths_.end(); I != IE; ++I) {
    AntidependencePathTy &Path = *I;
    for (AntidependencePathTy::iterator J = Path.begin(), JE = Path.end();
         J != JE; ++J) {
      Instruction *Candidate = *J;
      BasicBlock *CandidateBB = Candidate->getParent();
      CandidateInfo *&CI = CandidateInfoMap[Candidate];
      if (CI == NULL)
        CI = new CandidateInfo(Candidate,
                               LI_->getLoopDepth(CandidateBB),
                               isSubloopPreheader(*CandidateBB, *LI_));
      CI->add(Path);
    }
  }

  // Set up a worklist sorted by priority.  The highest priority candidates
  // will be at the back of the list.
  WorklistTy Worklist; 
  for (CandidateInfoMapTy::iterator I = CandidateInfoMap.begin(),
       E = CandidateInfoMap.end(); I != E; ++I)
    Worklist.push_back(I->second);
  std::sort(Worklist.begin(), Worklist.end(), CandidateInfo::compare);
  DEBUG(dumpWorklist(Worklist));

  // Process the candidates in the order that we see them popping off the back
  // of the worklist.
  while (!Worklist.empty()) {
    CandidateInfo *Info = Worklist.back();
    Worklist.pop_back();

    // Skip over candidates with no unintersected paths.
    if (Info->size() == 0)
      continue;

    // Pick this candidate and put it in the hitting set.
    DEBUG(dbgs() << "Picking "; Info->print(dbgs()));
    SmallPtrSet<Instruction *, 4> *Antideps = &CutMap_[Info->getCandidate()];
    CutSet_.insert(Info->getCandidate());

    // For each path that the candidate intersects, the other candidates that
    // also intersect that path now intersect one fewer unintersected paths.
    // Update those candidates (changes their priority) and intelligently
    // re-insert them into the worklist at the right place.
    for (CandidateInfo::const_iterator I = Info->begin(), IE = Info->end();
         I != IE; ++I) {
      DEBUG(dbgs() << " Processing redundant candidates for " << **I << "\n");
      Antideps->insert(*(*I)->begin());
      for (AntidependencePathTy::const_iterator J = (*I)->begin(),
           JE = (*I)->end(); J != JE; ++J)
        if (*J != Info->getCandidate())
          processRedundantCandidate(CandidateInfoMap[*J], &Worklist, **I);
    }
  }

  // Clean up.
  for (CandidateInfoMapTy::iterator I = CandidateInfoMap.begin(),
       E = CandidateInfoMap.end(); I != E; ++I)
    delete I->second;
}

static void dumpCandidate(const CandidateInfo &RedundantInfo,
                          const WorklistTy &Worklist) {
  dbgs() << "Redundant candidate in position ";
  WorklistTy::const_iterator It = std::lower_bound(
      Worklist.begin(),
      Worklist.end(),
      const_cast<CandidateInfo *>(&RedundantInfo),
      CandidateInfo::compare);
  while (*It != &RedundantInfo)
    ++It;
  dbgs() << (It - Worklist.begin() + 1) << "/" << Worklist.size();
  dbgs() << " " << getLocator(*RedundantInfo.getCandidate());
}

void MemoryIdempotenceAnalysisImpl::processRedundantCandidate(
    CandidateInfo *RedundantInfo,
    WorklistTy *Worklist,
    const AntidependencePathTy &Path) {
  DEBUG(dbgs() << "  Before: ";
        dumpCandidate(*RedundantInfo, *Worklist);
        dbgs() << "\n");

  // Find the place where the redundant candidate was in the worklist.  There
  // may be multiple candidates at the same priority so we may have to iterate
  // linearly a little bit.
  WorklistTy::iterator OldPosition = std::lower_bound(
    Worklist->begin(), Worklist->end(), RedundantInfo, CandidateInfo::compare);
  while (*OldPosition != RedundantInfo)
    ++OldPosition;

  // Remove the path and update the candidate's priority.  The worklist is now
  // no longer sorted.
  RedundantInfo->remove(Path);

  // Find the place to re-insert the redundant candidate in the worklist to make
  // it sorted again.
  WorklistTy::iterator NewPosition = std::lower_bound(
    Worklist->begin(), Worklist->end(), RedundantInfo, CandidateInfo::compare);
  assert(NewPosition <= OldPosition && "new position has higher priority");

  // Re-insert by rotation.
  std::rotate(NewPosition, OldPosition, next(OldPosition));
  DEBUG(dbgs() << "  After: ";
        dumpCandidate(*RedundantInfo, *Worklist);
        dbgs() << "\n");
}

void MemoryIdempotenceAnalysisImpl::print(raw_ostream &OS,
                                          const Module *M) const {
  OS << "\nMemoryIdempotenceAnalysis Cut Set:\n";
  for (MemoryIdempotenceAnalysis::const_iterator I = MIA_->begin(),
       E = MIA_->end(); I != E; ++I) {
    Instruction *Cut = *I;
    BasicBlock *CutBB = Cut->getParent();
    OS << "Cut at " << getLocator(*Cut) << " at loop depth "
      << LI_->getLoopDepth(CutBB) << "\n";
  }
  OS << "\n";
}

//===----------------------------------------------------------------------===//
// MemoryIdempotenceAnalysis
//===----------------------------------------------------------------------===//

char MemoryIdempotenceAnalysis::ID = 0;
INITIALIZE_PASS_BEGIN(MemoryIdempotenceAnalysis, "idempotence-analysis",
                "Idempotence Analysis", true, true)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_AG_DEPENDENCY(AliasAnalysis)
INITIALIZE_PASS_END(MemoryIdempotenceAnalysis, "idempotence-analysis",
                "Idempotence Analysis", true, true)

void MemoryIdempotenceAnalysis::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<AliasAnalysis>();
  AU.addRequired<DominatorTreeWrapperPass>();
  AU.addRequired<LoopInfoWrapperPass>();
  AU.setPreservesAll();
}

bool MemoryIdempotenceAnalysis::doInitialization(Module &M) {
  Impl = new MemoryIdempotenceAnalysisImpl(this);
  CutSet_ = &Impl->CutSet_;
  CutMap_ = &Impl->CutMap_;
  return false;
}

bool MemoryIdempotenceAnalysis::doFinalization(Module &M) {
  delete Impl;
  return false;
}

void MemoryIdempotenceAnalysis::releaseMemory() {
  Impl->releaseMemory();
}

bool MemoryIdempotenceAnalysis::runOnFunction(Function &F) {
  return Impl->runOnFunction(F);
}

void MemoryIdempotenceAnalysis::print(raw_ostream &OS, const Module *M) const {
  Impl->print(OS, M);
}

