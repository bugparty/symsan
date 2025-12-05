export KO_TARGET_LINE="19"
export KO_TARGET_FILE="dummy.c"
export KO_CTWM_INDEX_PATH="dummy_ctwm_index.json"
# KO_DONT_OPTIMIZE=1 ./build/bin/ko-clang ./dummy.c -S -emit-llvm -o dummy.ll -g
KO_DONT_OPTIMIZE=1 ../build/bin/ko-clang ./dummy.c -o dummy -g
