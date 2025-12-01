#ifndef SYMSAN_CTWM_INDEX_PASS_H
#define SYMSAN_CTWM_INDEX_PASS_H

#include "llvm/IR/PassManager.h"

namespace symsan {

class CTWMIndexPass : public llvm::PassInfoMixin<CTWMIndexPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M,
                              llvm::ModuleAnalysisManager &MAM);
  static bool isRequired() { return false; }
};

} // namespace symsan

#endif // SYMSAN_CTWM_INDEX_PASS_H
