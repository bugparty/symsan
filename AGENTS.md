# Repository Guidelines

## Project Structure & Module Organization
- `instrumentation/` holds LLVM passes (e.g., `TaintPass.cpp`, `CTWMIndexPass.cpp`) that insert symbolic tracking; `compiler/ko_clang.c` wraps clang to load them.
- `runtime/` packages DFSan-based runtime libraries and ABI lists; `backend/` and `solvers/` contain solving engines and helpers; `driver/` provides fuzzing/launcher shims (AFL++ plugin in `driver/aflpp/`).
- `include/` hosts shared headers; `python/` exposes bindings; `web-service/` is a small FastGen service wrapper; `tests/` contains lit regression cases; `examples/` offers minimal build/run samples.
- Build artifacts typically live in `build/`; installs can target `install/`.

## Build, Test, and Development Commands
- Configure (Release):  
  `CC=clang-14 CXX=clang++-14 cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$PWD/install -DAFLPP_PATH=$PWD/aflpp`
- Build and install:  
  `CC=clang-14 CXX=clang++-14 cmake --build build`  
  `CC=clang-14 CXX=clang++-14 cmake --install build`
- Docker option: `docker build -t symsan .`
- Quick sample builds: `examples/*_build.sh` wrap the compiler and runtime for demo programs.

## Coding Style & Naming Conventions
- Follow the existing LLVM/DFSan style: 2-space indents, brace on the same line, keep includes grouped (`llvm/*`, system, then project headers) and alphabetized.
- Prefer descriptive snake_case for C functions/files and CamelCase for C++ types mirroring LLVM idioms; keep public headers lean and documented.
- Avoid introducing new runtime dependencies; reuse helpers under `runtime/sanitizer_common/` when possible.

## Testing Guidelines
- Use `lit` for regression tests: from the build tree run `pip install lit` once, then `lit --verbose tests`.
- Add new tests under `tests/` with small, focused `.c`/`.cpp` programs; keep names descriptive (e.g., `boundary8.c`, `trace_bb.c`) and ensure they compile in CI with default CMake options.
- For features touching runtime or passes, include a reproducer in `examples/` or extend an existing lit test to cover the new path.

## Commit & Pull Request Guidelines
- Commit messages follow an imperative, prefix-friendly style seen in history (e.g., `fix: ...`, `add ...`, `improve ...`); keep the subject under ~72 chars.
- PRs should describe the motivation, key design notes, and testing performed (`lit --verbose tests`, sample program output). Link related issues and note any flags or env vars required.
- Include screenshots or logs only when behavior is user-facing (e.g., web-service responses or CLI output); otherwise keep diffs reproducible via commands.

## Configuration & Runtime Tips
- Set `KO_CC`/`KO_CXX` if your default compilers are not clang-14; toggle solver/backend behavior with `KO_USE_Z3`, `KO_USE_NATIVE_LIBCXX`, and optimization with `KO_DONT_OPTIMIZE`.
- CTWM support: configure with `-DSYMSAN_CTWM_ENABLE_INDEX=ON` and runtime trace paths via `SYMSAN_CTWM_TRACE_PATH=/tmp/trace.bin`; `ko-clang` picks up fresh passes from the build tree, so reinstalling is usually unnecessary while iterating.
