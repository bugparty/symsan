export KO_TARGET_LINE="120"
export KO_TARGET_FILE="./control_temp.c"
export KO_CTWM_INDEX_PATH="control_temp_ctwm_index.json"
# KO_DONT_OPTIMIZE=1 ./build/bin/ko-clang ./dummy.c -S -emit-llvm -o dummy.ll -g
KO_DONT_OPTIMIZE=1 ../build/bin/ko-clang ./control_temp.c -o control_temp -g