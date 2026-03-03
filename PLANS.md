# PLANS.md — EasyWork Pending Backlog Only

Updated: 2026-03-03

This file keeps only unfinished work. Completed cleanup/contract changes are documented in README and `doc/`.

## 0. Goals and Constraints

- Development mode can use Python for fast iteration.
- Production mode forbids Python runtime (`EASYWORK_BUILD_PYTHON=OFF`), C++ Runtime + IR only.
- Cross/offline build must remain available (`EASYWORK_FETCH_DEPS=OFF`).
- AI-assisted changes must be gate-verifiable (build/test/replay/graph validation).

## 1. P1 (Must finish before release)

### 1.1 Runtime determinism and resource control
- [ ] Standardize backpressure policy (block/drop/downsample).
- [ ] Define timeout policy and queue-cap policy.
- [ ] Document memory budget and zero-copy constraints; add tests.
- [ ] Add deployment profiles for thread/CPU affinity.

### 1.2 Embedded replay closed loop
- [ ] Define replay package format (binary + graph + sample + expected metrics).
- [ ] Implement target replay runner (throughput/latency/drop/error output).
- [ ] Implement host comparator and gate integration.

### 1.3 IR governance
- [ ] Define forward/backward compatibility strategy for `schema_version`.
- [ ] Add IR change log and rollback strategy.

### 1.4 Unified config system
- [ ] Unify config sources (CLI/env/file) and precedence.
- [ ] Add config version validation and default-value governance.

## 2. P2 (Continuous evolution)

### 2.1 Python Node -> C++ Node AI migration system
- [ ] Add `doc/node_contract.md` (types/args/timestamp/exception/order semantics).
- [ ] Build golden harness (Python reference vs C++ implementation).
- [ ] Add migration playbook (prototype -> translate -> verify -> release).

### 2.2 Security and supply chain
- [ ] Add IR input validation and allowlist policy.
- [ ] Add SBOM/signing/license/reproducible-build checks.
- [ ] Add plugin/node registration allowlist.

### 2.3 Hardware acceleration roadmap
- [ ] Define abstraction boundary for CPU/OpenCV/CUDA/NPU/V4L2/GStreamer.
- [ ] Add backend capability negotiation and deployment profiles.
- [ ] Add backend replay baselines.

### 2.4 Team and AI collaboration conventions
- [ ] Add PR template and required gates checklist.
- [ ] Define AI change granularity rules.
- [ ] Add protected-directory policy (toolchain/schema/release scripts).

## 3. Milestones (Pending Only)

### M3 (2026-04-10) — Runtime Stability Baseline
Scope: P1.1 + P1.2

### M4 (2026-04-25) — IR + Config Governance
Scope: P1.3 + P1.4

### M5 (2026-05-20) — AI Migration Engineering
Scope: P2.1

### M6 (2026-06-20) — Security + Hardware + Team Rules
Scope: P2.2 + P2.3 + P2.4

## 4. Baseline Validation Commands

Runtime-only:
```bash
cmake -S . -B build_rt -DEASYWORK_BUILD_PYTHON=OFF -DEASYWORK_WITH_OPENCV=OFF
cmake --build build_rt
ctest --test-dir build_rt --output-on-failure
./build_rt/easywork_runtime_example
./build_rt/easywork-run --graph /tmp/pipeline.json
```

Full/dev:
```bash
cmake -S . -B build_full -DEASYWORK_BUILD_PYTHON=ON -DEASYWORK_WITH_OPENCV=ON
cmake --build build_full
ctest --test-dir build_full --output-on-failure
PYTHONPATH=python python -m pytest tests
```

Cross:
```bash
cmake --preset cross-aarch64-release
cmake --build build/cross-aarch64-release
```

## 5. Current Risks

- Some environments may not have an available aarch64 cross toolchain.

