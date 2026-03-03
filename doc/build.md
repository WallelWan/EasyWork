# Build Matrix and Gate Scripts

## Minimum Compiler Matrix

- GCC: 10+
- Clang: 12+
- MSVC: 19.29+ (VS2019 16.11+)

These checks are enforced in `CMakeLists.txt` (fail-fast on configure).

## Build Profiles

### Dev / Full build (Python + OpenCV)

```bash
cmake -S . -B build_full \
  -DEASYWORK_BUILD_PYTHON=ON \
  -DEASYWORK_WITH_OPENCV=ON
cmake --build build_full
ctest --test-dir build_full --output-on-failure
PYTHONPATH=python python -m pytest tests
```

### Prod / Runtime-only build

```bash
cmake -S . -B build_rt \
  -DEASYWORK_BUILD_PYTHON=OFF \
  -DEASYWORK_WITH_OPENCV=OFF \
  -DEASYWORK_FETCH_DEPS=OFF
cmake --build build_rt
ctest --test-dir build_rt --output-on-failure
```

### Cross build (aarch64 template)

```bash
cmake --preset cross-aarch64-release
cmake --build build/cross-aarch64-release
```

When the cross compiler is absent, gate scripts should skip with a clear message.

## CI Gate Scripts

- Dev gate: `scripts/ci/dev_gate.sh`
- Prod gate: `scripts/ci/prod_gate.sh`
- Cross gate: `scripts/ci/cross_gate.sh`
- Production graph validator: `scripts/check_production_graph.py`
- Production artifact validator: `scripts/check_production_artifacts.sh`

Examples:

```bash
scripts/ci/dev_gate.sh
scripts/ci/prod_gate.sh
scripts/ci/cross_gate.sh
python scripts/check_production_graph.py --graph /tmp/pipeline.json
# optional: ARTIFACT_DIR=/tmp/release scripts/ci/prod_gate.sh
```
