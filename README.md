[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)

# SymSan: Time and Space Efficient Concolic Execution via Dynamic Data-Flow Analysis

SymSan (Symbolic Sanitizer) is an efficient concolic execution engine based on the
Data-Floow Sanitizer (DFSan) framework. By modeling forward symbolic execution as
a dynamic data-flow analysis and leveraging the time and space efficient data-flow
tracking infrastructure from DFSan, SymSan imposes much lower runtime overhead
than previous symbolic execution engines.

Similar to other compilation-based symbolic executor like [SymCC](https://github.com/eurecom-s3/symcc),
SymSan uses compile-time `instrumentation` to insert symbolic execution logic into
the target program, and a `runtime` supporting library to maintain symbolic states
during execution.

To learn more, checkout our [paper](https://www.usenix.org/conference/usenixsecurity22/presentation/chen-ju) at USENIX Security 2022.

## Building

Because SymSan leverages the shadow memory implementation from LLVM's sanitizers,
it has more strict dependency on the LLVM version. Right now only LLVM 12 is tested.

### Build Requirements

- Linux-amd64 (Tested on Ubuntu 24.04)
- [LLVM 14.0.6](http://llvm.org/docs/index.html): clang, libc++, libc++abi

### Compilation

Create a `build` directory and execute the following commands in it:

```shell
$ CC=clang-14 CXX=clang++-14 cmake -DCMAKE_INSTALL_PREFIX=/path/to/install -DCMAKE_BUILD_TYPE=Release /path/to/symsan/source
$ make
$ make install
```

### Build in Docker

```
docker build -t symsan .
```

### LIBCXX

The repo contains instrumented libc++ and libc++abi to support C++ programs.
To rebuild these libraries from source, execute the `rebuild.sh` script in the
`libcxx` directory.

**NOTE**: because the in-process solving module (`solver/z3.cpp`) uses Z3's C++ API
and STL containers, so itself depends on the C++ libs. Due to such dependencies,
you'll see linking errors when building C++ targets when using this module.
Though it's possible to resolve these errors by not instrumenting the dependencies
(adding them to the [ABIList](https://clang.llvm.org/docs/DataFlowSanitizer.html#abi-list),
 then rebuild the C++ libs), we don't recommend using it for C++ targets.
Instead, it's much cleaner to use ann out-of-process solving module like Fastgen.

## Test

To verify the code works, try some simple tests
(forked from [Angora](https://github.com/AngoraFuzzer/Angora),
adapted by [@insuyun](https://github.com/insuyun) to lit):

```
$ pip install lit
$ cd your_build_dir
$ lit tests
```

### Environment Options

* `KO_CC` specifies the clang to invoke, if the default version isn't clang-12,
  set this variable to allow the compiler wrapper to find the correct clang.

* `KO_CXX` specifies the clang++ to invoke, if the default version isn't clang++-12,
  set this variable to allow the compiler wrapper to find the correct clang++.

* `KO_USE_Z3` enables the in-process Z3-based solver. By default, it is disabled,
  so SymSan will only perform symbolic constraint collection without solving.
  SymSan also supports out-of-process solving, which provides better compatiblility.
  Check [FastGen](https://github.com/R-Fuzz/fastgen).

* `KO_USE_NATIVE_LIBCXX` enables using the native uninstrumented libc++ and libc++abi.

* `KO_DONT_OPTIMIZE` don't override the optimization level to `O3`.

### CTWM Indexing & Trace

SymSan can emit CTWM-friendly metadata and runtime traces. Configure these
features at CMake time or per compilation:

* `SYMSAN_CTWM_ENABLE_INDEX` (default `ON`) builds the LLVM
  `CTWMIndexPass`, which records basic-block IDs and the SymSan branch IDs found
  by `__taint_trace_cond`. Each compile produces `ctwm_index.json` beside the
  compiler’s working directory. Override the output path with
  `KO_CTWM_INDEX_PATH=/tmp/foo.json`.
* `SYMSAN_CTWM_ENABLE_BB_TRACE` (default `OFF`) injects calls to
  `__ctwm_trace_bb` on every basic block. Per-build overrides are available via
  `KO_CTWM_ENABLE_BB_TRACE` and `KO_CTWM_DISABLE_BB_TRACE`.
* Use `KO_CTWM_ENABLE_INDEX` / `KO_CTWM_DISABLE_INDEX` when you need to toggle
  the index pass without reconfiguring CMake.
* Runtime traces are written by the new `ctwm_trace` runtime component. Set
  `SYMSAN_CTWM_TRACE_PATH=/tmp/trace.bin` (defaults to `ctwm_trace.log`) before
  running the instrumented binary. The log is a binary stream of `int32_t`
  basic-block IDs and aligns with the IDs stored in the JSON index.
* When experimenting from the build tree, `ko-clang` automatically prefers the
  freshly built `instrumentation/TaintPass.so`, so you don’t need to run
  `make install` just to regenerate `ctwm_index.json`.
* For diagnostics you can export `SYMSAN_CTWM_DEBUG=1` to have the pass report
  where it writes the index and whether trace hooks were injected.

### Hybrid Fuzzing

SymSan needs a driver to perform hybrid fuzzing, like [FastGen](https://github.com/R-Fuzz/fastgen).
It could also be used as a custom mutator for [AFL++](https://github.com/AFLplusplus/AFLplusplus)
(check the [plugin readme](driver/aflpp/README.md)).

## Documentation

Still under construction, unfortunately. [DeepWiki](https://deepwiki.com/R-Fuzz/symsan) seems okay.

## Reference

To cite SymSan in scientific work, please use the following BibTeX:

``` bibtex
@inproceedings {chen2022symsan,
  author =       {Ju Chen and Wookhyun Han and Mingjun Yin and Haochen Zeng and
                  Chengyu Song and Byoungyong Lee and Heng Yin and Insik Shin},
  title =        {SymSan: Time and Space Efficient Concolic Execution via Dynamic Data-Flow Analysis},
  booktitle =    {{USENIX} Security Symposium (Security)},
  year =         2022,
  url =          {https://www.usenix.org/conference/usenixsecurity22/presentation/chen-ju},
  publisher =    {{USENIX} Association},
  month =        aug,
}
```
