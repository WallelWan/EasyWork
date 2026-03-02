# Cross Build Guide (Runtime-only)

## 1. Prepare dependencies on host
For offline/cross scenarios, set:
- `EASYWORK_FETCH_DEPS=OFF`

Install required packages/toolchain beforehand:
- CMake/Ninja
- Cross compiler (for example `aarch64-linux-gnu-g++`)
- `nlohmann_json` available to CMake in target/sysroot path
- Ensure the compiler exists in `PATH` or set full path via `CMAKE_CXX_COMPILER`.

## 2. Configure
Using preset template:
```bash
cmake --preset cross-aarch64-release
```

Or explicit configure:
```bash
cmake -S . -B build/cross-aarch64-release \
  -DCMAKE_TOOLCHAIN_FILE=toolchains/aarch64-generic.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DEASYWORK_BUILD_PYTHON=OFF \
  -DEASYWORK_WITH_OPENCV=OFF \
  -DEASYWORK_FETCH_DEPS=OFF
```

## 3. Build and test
```bash
cmake --build build/cross-aarch64-release
ctest --test-dir build/cross-aarch64-release
```

## 4. Deploy and run
Copy runtime binaries to target device, then run:
```bash
./easywork_runtime_example
./easywork-run --graph pipeline.json
```

## Notes
- `easywork-run` executes graph JSON without Python at runtime.
- If your cross environment cannot run tests on host, execute tests on target device.
