# Repository Guidelines

## Project Structure & Modules
- C/C++ core (instrumentation, solvers, runtime) lives under `compiler/`, `instrumentation/`, `runtime/`, `driver/`, `backend/`, `solvers/`, `parsers/`, `include/`, with build artifacts placed in `build/`.
- Python bindings and tooling sit in `python/`.
- Web API wrapper is in `web-service/` (FastAPI app, Docker/compose assets, sample clients/tests).
- Examples and fixtures are under `examples/`; tests under `tests/`.

## Build, Test, and Development Commands
- Native build (from repo root): `mkdir -p build && cd build && CC=clang-14 CXX=clang++-14 cmake -DCMAKE_BUILD_TYPE=Debug .. && make -j$(nproc)`. Installs into `build/` by default.
- Docker builder (core + artifacts): `./web-service/build_docker.sh` builds `symsan-builder` then `symsan-web-service` via buildx.
- Web service local dev: `cd web-service && pip install -r requirements.txt && uvicorn app:app --reload --host 0.0.0.0 --port 8000`.
- Web service API smoke test (service must be running): `API_URL=http://localhost:11000 bash web-service/test_api.sh`.
- Compose up: `docker compose -f web-service/compose.yml up -d` (exposes port 11000 by default).

## Coding Style & Naming
- C/C++: prefer clang/LLVM defaults; keep headers in `include/`, sources in module directories; namespaced, lower_snake_case for functions/files, UpperCamel for types; avoid non-ASCII identifiers.
- Python: black/PEP8 style; modules lower_snake_case; keep logs concise.
- Shell: `set -euo pipefail`, bash shebang, avoid relative paths when writing files.

## Testing Guidelines
- Core: after building, run from `build/`: `ctest` (add `-j` as needed). Add deterministic tests in `tests/`.
- Web API: `web-service/test_api.sh` against a running service; adjust `API_URL` if port changes. Inspect `web-service/results/<task_id>/execution.log` for fgtest failures.
- When adding tests, prefer small fixtures in `examples/` and keep runtime under a few seconds.

## Commit & PR Guidelines
- Commit messages: short imperative summary (e.g., “Fix fgtest trace parsing”), keep scope focused.
- PRs should describe intent, key changes, testing performed (`ctest`, `test_api.sh`, manual steps), and note any config/env vars required (e.g., `FGTEST_BIN_DIR`, `FGTEST_PATH`).
- Include screenshots/log snippets for user-facing or API changes when helpful.
