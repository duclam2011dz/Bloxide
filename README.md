# Bloxide

Bloxide is a C++17 voxel prototype using SDL2 and OpenGL 3.3 Core. Version 1.1 adds an infinite flat world streamed in 16x256x16 chunks.

## v1.1 features

- Infinite signed `int64` chunk coordinates with correct negative floor division.
- Deterministic terrain: Bedrock at Y=0, Stone Y=1..61, Dirt Y=62, Grass Y=63 and Air above.
- Generation distance 10, simulation distance 6 and render distance 8 using Chebyshev chunk distance.
- Two generation workers and two meshing workers with priority queues, cancellation and chunk states.
- Hidden-face removal, greedy meshing, packed 32-bit vertices, uint32 index buffers and simple LOD1.
- Main-thread-only OpenGL upload with per-frame streaming work bounded by the render loop.
- In-memory edit journal; Bedrock cannot be removed.
- `BloxideBenchmark` emits CSV, JSON summary and Chrome trace files.

## Build

Supported environment: Windows/MSYS2 UCRT64 with GCC, CMake and SDL2. Configure with the official CMake executable if the MSYS2 CMake binary crashes:

```powershell
cmake -S . -B build-cmake -G "MinGW Makefiles" `
  -DCMAKE_C_COMPILER=D:/msys64/ucrt64/bin/gcc.exe `
  -DCMAKE_CXX_COMPILER=D:/msys64/ucrt64/bin/g++.exe `
  -DCMAKE_MAKE_PROGRAM=D:/msys64/ucrt64/bin/mingw32-make.exe `
  -DCMAKE_PREFIX_PATH=D:/msys64/ucrt64 -DCMAKE_BUILD_TYPE=Release
cmake --build build-cmake --parallel 2
ctest --test-dir build-cmake --output-on-failure
```

Run the game from the repository root so shader paths resolve:

```powershell
build-cmake/Bloxide.exe
```

## Benchmark

```powershell
build-cmake/BloxideBenchmark.exe
build-cmake/BloxideBenchmark.exe --quick
```

Results are written under `build/benchmark`: `v1.1-runtime.csv`, `v1.1-summary.json`, `v1.1-trace.json` and `v1.1-metrics.txt`. The runtime game writes the same profiler formats after exit. Press `F3` in a future HUD-enabled build to display runtime counters; the current window title reports the active chunk/draw counters when the HUD is enabled in source.

## Controls

WASD moves, mouse looks, Space jumps, left mouse removes a breakable block, right mouse places grass, and Esc releases or captures the mouse.

## Project documents

- [ARCHITECTURES.md](ARCHITECTURES.md)
- [PERFORMANCE.md](PERFORMANCE.md)
- [CHANGELOG.md](CHANGELOG.md)
- [LICENSE](LICENSE)
