#include "CTWMIndexPass.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#define DEBUG_TYPE "ctwm-index"

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdlib>
#include <map>
#include <string>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

using namespace llvm;
namespace json = llvm::json;

namespace {

cl::opt<std::string> ClCTWMIndexOutput(
    "symsan-ctwm-index-out",
    cl::desc("Path to the CTWM index JSON file (default: ctwm_index.json)"),
    cl::Hidden, cl::init("ctwm_index.json"));

cl::opt<bool> ClCTWMEnableIndex(
    "symsan-ctwm-enable-index",
    cl::desc("Force-enable CTWM index emission regardless of build default"),
    cl::Hidden, cl::init(false));

cl::opt<bool> ClCTWMDisableIndex(
    "symsan-ctwm-disable-index",
    cl::desc("Disable CTWM index emission regardless of build default"),
    cl::Hidden, cl::init(false));

cl::opt<bool> ClCTWMEnableBBTrace(
    "symsan-ctwm-enable-bb-trace",
    cl::desc(
        "Force-enable CTWM basic block trace instrumentation in the module"),
    cl::Hidden, cl::init(false));

cl::opt<bool> ClCTWMDisableBBTrace(
    "symsan-ctwm-disable-bb-trace",
    cl::desc(
        "Disable CTWM basic block trace instrumentation regardless of build default"),
    cl::Hidden, cl::init(false));

struct BasicBlockRecord {
  uint32_t Id = 0;
  std::string Function;
  std::string Name;
  bool IsEntry = false;
};

struct BranchRecord {
  uint32_t BranchBB = 0;
  uint32_t TrueBB = 0;
  uint32_t FalseBB = 0;
  int32_t SymSanId = 0;
  std::string File;
  unsigned Line = 0;
  unsigned Column = 0;
  std::string Function;
};

struct SourceGroupKey {
  std::string File;
  unsigned Line = 0;
  unsigned Column = 0;
  std::string Function;

  bool operator<(const SourceGroupKey &Other) const {
    return std::tie(File, Line, Column, Function) <
           std::tie(Other.File, Other.Line, Other.Column, Other.Function);
  }
};

struct SourceGroup {
  std::string File;
  unsigned Line = 0;
  unsigned Column = 0;
  std::string Function;
  SmallVector<size_t, 2> BranchIndices;
};

bool wantIndexEmission() {
#if SYMSAN_CTWM_ENABLE_INDEX
  const bool EnabledByBuild = true;
#else
  const bool EnabledByBuild = false;
#endif
  if (ClCTWMDisableIndex)
    return false;
  if (ClCTWMEnableIndex)
    return true;
  return EnabledByBuild;
}

bool wantBBTraceInstrumentation() {
#if SYMSAN_CTWM_ENABLE_BB_TRACE
  const bool EnabledByBuild = true;
#else
  const bool EnabledByBuild = false;
#endif
  if (ClCTWMDisableBBTrace)
    return false;
  if (ClCTWMEnableBBTrace)
    return true;
  return EnabledByBuild;
}

bool isDebugLoggingEnabled() {
  static int Enabled = []() {
    const char *Env = std::getenv("SYMSAN_CTWM_DEBUG");
    return (Env && *Env) ? 1 : 0;
  }();
  return Enabled != 0;
}

std::string getFunctionDisplayName(const Function &F) {
  if (const DISubprogram *SP = F.getSubprogram()) {
    if (!SP->getName().empty())
      return SP->getName().str();
    if (!SP->getLinkageName().empty())
      return SP->getLinkageName().str();
  }
  return F.getName().str();
}

std::string buildFilePath(const DIFile *File) {
  if (!File)
    return {};
  SmallString<256> Path(File->getFilename());
  StringRef Dir = File->getDirectory();
  if (!Dir.empty()) {
    SmallString<256> Combined(Dir);
    sys::path::append(Combined, File->getFilename());
    return std::string(Combined.str());
  }
  return std::string(Path.str());
}

void fillSourceInfo(const BranchInst &Br, BranchRecord &Record) {
  if (const DebugLoc &DL = Br.getDebugLoc()) {
    Record.Line = DL.getLine();
    Record.Column = DL.getCol();
    const DIScope *Scope = dyn_cast_or_null<DIScope>(DL.getScope());
    const DIFile *File = Scope ? Scope->getFile() : nullptr;
    Record.File = buildFilePath(File);
  }
}

int32_t findSymSanId(const BranchInst &Br) {
  const Value *Cond = Br.getCondition();
  const BasicBlock *BB = Br.getParent();
  for (const Instruction &I : *BB) {
    if (&I == &Br)
      break;
    const auto *Call = dyn_cast<CallBase>(&I);
    if (!Call)
      continue;
    const Function *Callee = Call->getCalledFunction();
    if (!Callee)
      continue;
    if (Callee->getName() != "__taint_trace_cond")
      continue;
    if (Call->arg_size() < 4)
      continue;
    if (Call->getArgOperand(1) != Cond)
      continue;
    if (const auto *Const = dyn_cast<ConstantInt>(Call->getArgOperand(3)))
      return static_cast<int32_t>(Const->getSExtValue());
  }
  return 0;
}

void assignBasicBlockIds(Module &M,
                         DenseMap<const BasicBlock *, uint32_t> &Mapping,
                         std::vector<BasicBlockRecord> &Records) {
  uint32_t NextId = 1;
  for (Function &F : M) {
    if (F.isDeclaration())
      continue;
    std::string FuncName = getFunctionDisplayName(F);
    for (BasicBlock &BB : F) {
      BasicBlockRecord Record;
      Record.Id = NextId;
      Record.Function = FuncName;
      Record.Name = BB.getName().str();
      Record.IsEntry = &BB == &F.getEntryBlock();
      Mapping[&BB] = NextId++;
      Records.push_back(std::move(Record));
    }
  }
}

void collectBranchRecords(Module &M,
                          const DenseMap<const BasicBlock *, uint32_t> &Mapping,
                          std::vector<BranchRecord> &Records,
                          std::vector<SourceGroup> &Groups) {
  std::map<SourceGroupKey, SourceGroup> GroupMap;
  for (Function &F : M) {
    if (F.isDeclaration())
      continue;
    std::string FuncName = getFunctionDisplayName(F);
    for (BasicBlock &BB : F) {
      for (Instruction &I : BB) {
        auto *Br = dyn_cast<BranchInst>(&I);
        if (!Br || !Br->isConditional())
          continue;
        auto It = Mapping.find(&BB);
        if (It == Mapping.end())
          continue;
        if (Br->getNumSuccessors() < 2)
          continue;
        BranchRecord Record;
        Record.BranchBB = It->second;
        if (const BasicBlock *TrueBB = Br->getSuccessor(0)) {
          auto TrueIt = Mapping.find(TrueBB);
          if (TrueIt != Mapping.end())
            Record.TrueBB = TrueIt->second;
        }
        if (const BasicBlock *FalseBB = Br->getSuccessor(1)) {
          auto FalseIt = Mapping.find(FalseBB);
          if (FalseIt != Mapping.end())
            Record.FalseBB = FalseIt->second;
        }
        Record.SymSanId = findSymSanId(*Br);
        Record.Function = FuncName;
        fillSourceInfo(*Br, Record);

        size_t Index = Records.size();
        Records.push_back(std::move(Record));

        const BranchRecord &Inserted = Records.back();
        SourceGroupKey Key{Inserted.File, Inserted.Line, Inserted.Column,
                           Inserted.Function};
        SourceGroup &Group = GroupMap[Key];
        Group.File = Inserted.File;
        Group.Line = Inserted.Line;
        Group.Column = Inserted.Column;
        Group.Function = Inserted.Function;
        Group.BranchIndices.push_back(Index);
      }
    }
  }

  Groups.reserve(GroupMap.size());
  for (auto &Entry : GroupMap)
    Groups.push_back(std::move(Entry.second));
}

bool writeIndexJSON(Module &M, ArrayRef<BasicBlockRecord> BasicBlocks,
                    ArrayRef<BranchRecord> Branches,
                    ArrayRef<SourceGroup> Groups) {
  const bool EmitIndex = wantIndexEmission();
  if (isDebugLoggingEnabled())
    errs() << "CTWMIndexPass: wantIndexEmission=" << EmitIndex << "\n";
  if (!EmitIndex)
    return false;

  std::string OutPath = ClCTWMIndexOutput;
  if (OutPath.empty())
    OutPath = "ctwm_index.json";

  if (OutPath != "-") {
    SmallString<256> DirPath(OutPath);
    StringRef Parent = sys::path::parent_path(DirPath);
    if (!Parent.empty()) {
      std::error_code DirEC = sys::fs::create_directories(Parent);
      const std::error_code FileExists =
          std::make_error_code(std::errc::file_exists);
      const std::error_code NotDirectory =
          std::make_error_code(std::errc::not_a_directory);
      if (DirEC && DirEC != FileExists && DirEC != NotDirectory) {
        errs() << "CTWMIndexPass: failed to create directory for " << OutPath
               << ": " << DirEC.message() << "\n";
        return false;
      }
    }
  }

  std::error_code EC;
  raw_fd_ostream OS(OutPath, EC, sys::fs::OF_Text);
  if (EC) {
    errs() << "CTWMIndexPass: failed to open " << OutPath << ": "
           << EC.message() << "\n";
    return false;
  }

  StringRef ModuleId = M.getModuleIdentifier();
  std::string ModuleName;
  if (ModuleId.empty())
    ModuleName = "module";
  else
    ModuleName = sys::path::filename(ModuleId).str();

  json::Object Root;
  Root["version"] = 1;
  Root["module"] = ModuleName;

  json::Array BasicBlockArray;
  for (const auto &Record : BasicBlocks) {
    json::Object BBObj;
    BBObj["id"] = static_cast<int64_t>(Record.Id);
    if (!Record.Function.empty())
      BBObj["function"] = Record.Function;
    if (!Record.Name.empty())
      BBObj["name"] = Record.Name;
    if (Record.IsEntry)
      BBObj["is_entry"] = true;
    BasicBlockArray.emplace_back(std::move(BBObj));
  }
  Root["basic_blocks"] = std::move(BasicBlockArray);

  json::Array BranchArray;
  for (const auto &Record : Branches) {
    json::Object BrObj;
    if (!Record.File.empty())
      BrObj["file"] = Record.File;
    if (Record.Line)
      BrObj["line"] = static_cast<int64_t>(Record.Line);
    if (Record.Column)
      BrObj["column"] = static_cast<int64_t>(Record.Column);
    if (!Record.Function.empty())
      BrObj["function"] = Record.Function;
    BrObj["bb"] = static_cast<int64_t>(Record.BranchBB);
    BrObj["succ_true"] = static_cast<int64_t>(Record.TrueBB);
    BrObj["succ_false"] = static_cast<int64_t>(Record.FalseBB);
    BrObj["symSanId"] = static_cast<int64_t>(Record.SymSanId);
    BranchArray.emplace_back(std::move(BrObj));
  }
  Root["branches"] = std::move(BranchArray);

  json::Array GroupArray;
  for (const auto &Group : Groups) {
    if (Group.BranchIndices.empty())
      continue;
    json::Object GroupObj;
    if (!Group.File.empty())
      GroupObj["file"] = Group.File;
    if (Group.Line)
      GroupObj["line"] = static_cast<int64_t>(Group.Line);
    if (Group.Column)
      GroupObj["column"] = static_cast<int64_t>(Group.Column);
    if (!Group.Function.empty())
      GroupObj["function"] = Group.Function;
    json::Array SymIds;
    json::Array Chain;
    for (size_t Index : Group.BranchIndices) {
      const auto &Record = Branches[Index];
      SymIds.emplace_back(static_cast<int64_t>(Record.SymSanId));
      json::Object ChainObj;
      ChainObj["bb"] = static_cast<int64_t>(Record.BranchBB);
      ChainObj["succ_true"] = static_cast<int64_t>(Record.TrueBB);
      ChainObj["succ_false"] = static_cast<int64_t>(Record.FalseBB);
      ChainObj["symSanId"] = static_cast<int64_t>(Record.SymSanId);
      Chain.emplace_back(std::move(ChainObj));
    }
    GroupObj["symSanIds"] = std::move(SymIds);
    GroupObj["branches"] = std::move(Chain);
    GroupArray.emplace_back(std::move(GroupObj));
  }
  Root["if_groups"] = std::move(GroupArray);

  json::Value RootValue(std::move(Root));
  OS << formatv("{0:2}", RootValue) << '\n';

  if (isDebugLoggingEnabled())
    errs() << "CTWMIndexPass: wrote index to " << OutPath << "\n";
  return true;
}

bool instrumentBasicBlocks(Module &M,
                           const DenseMap<const BasicBlock *, uint32_t> &Mapping) {
  const bool Instrument = wantBBTraceInstrumentation();
  if (isDebugLoggingEnabled())
    errs() << "CTWMIndexPass: wantBBTrace=" << Instrument << "\n";
  if (!Instrument)
    return false;

  LLVMContext &Ctx = M.getContext();
  FunctionCallee TraceFn = M.getOrInsertFunction(
      "__ctwm_trace_bb", FunctionType::get(Type::getVoidTy(Ctx),
                                           {Type::getInt32Ty(Ctx)}, false));
  if (Function *Fn = dyn_cast<Function>(TraceFn.getCallee())) {
    Fn->setDoesNotThrow();
    Fn->addFnAttr(Attribute::NoUnwind);
    Fn->addFnAttr(Attribute::NoInline);
  }

  bool Changed = false;
  for (Function &F : M) {
    if (F.isDeclaration())
      continue;
    if (F.getName() == "__ctwm_trace_bb")
      continue;
    for (BasicBlock &BB : F) {
      auto It = Mapping.find(&BB);
      if (It == Mapping.end())
        continue;

      Instruction *InsertPt = BB.getFirstNonPHI();
      if (!InsertPt)
        InsertPt = BB.getTerminator();
      if (!InsertPt)
        continue;

      IRBuilder<> IRB(InsertPt);
      Value *IdValue =
          ConstantInt::get(Type::getInt32Ty(Ctx), It->second, false);
      CallInst *Call = IRB.CreateCall(TraceFn, {IdValue});
      Call->setTailCallKind(CallInst::TCK_NoTail);
      Changed = true;
    }
  }
  return Changed;
}

} // namespace

namespace symsan {

PreservedAnalyses
CTWMIndexPass::run(Module &M, ModuleAnalysisManager &MAM) {
  if (isDebugLoggingEnabled())
    errs() << "CTWMIndexPass: running on " << M.getModuleIdentifier() << "\n";
  DenseMap<const BasicBlock *, uint32_t> IdMapping;
  std::vector<BasicBlockRecord> BlockRecords;
  assignBasicBlockIds(M, IdMapping, BlockRecords);
  if (isDebugLoggingEnabled())
    errs() << "CTWMIndexPass: assigned " << BlockRecords.size()
           << " basic block ids\n";
  if (BlockRecords.empty())
    return PreservedAnalyses::all();

  std::vector<BranchRecord> BranchRecords;
  std::vector<SourceGroup> Groups;
  collectBranchRecords(M, IdMapping, BranchRecords, Groups);

  bool ChangedIR = false;
  writeIndexJSON(M, BlockRecords, BranchRecords, Groups);
  ChangedIR |= instrumentBasicBlocks(M, IdMapping);

  if (ChangedIR)
    return PreservedAnalyses::none();
  return PreservedAnalyses::all();
}

} // namespace symsan
