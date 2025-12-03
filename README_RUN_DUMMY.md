First, build symsan with:
```
cd /work/symsan/ && mkdir -p build && \
    cd build && CC=clang-14 CXX=clang++-14 cmake -DCMAKE_INSTALL_PREFIX=. -DAFLPP_PATH=/work/aflpp -DCMAKE_BUILD_TYPE=Debug ../  && \
    make -j `nproc` && make install
```

Then run
```
cd examples
bash dummy_build.sh
bash dummy_reward.sh
```

You will get `rewards.json`.

You will get `"provided_steps": 2` is because `trace_fsize` is disabled at `runtime/dfsan/dfsan_flags.inc`.
So only 2 if branches are tested. The branch that checks fread size is ignored.