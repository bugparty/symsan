# KO_DONT_OPTIMIZE=1 ./build/bin/ko-clang ./dummy.c -S -emit-llvm -o dummy.ll -g
KO_DONT_OPTIMIZE=1 ./build/bin/ko-clang ./dummy.c -o dummy -g
