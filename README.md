# Bloxide

Bloxide is a C++17 voxel prototype using SDL2 and OpenGL 3.3 Core. Version 1.1.5 fixes terrain rendering and introduces block-based player physics in the streamed 16x256x16 world.

## v1.1.5 bug fixes and physics

- Terrain vertices retain chunk boundaries X/Z=16 and Y=256; top/bottom faces use outward winding with back-face culling.
- Player uses a 0.6x1.8x0.6 AABB, eye offset 1.62, gravity 24 blocks/s², terminal speed 40 blocks/s and ground-only jump speed 7 blocks/s.
- Collision resolves all three axes against solid blocks with substeps of at most 5 ms, including walls, ceilings, corners and chunk seams.
- Spawn waits for chunk data and finds the actual supporting surface. Unloaded columns block movement until generation completes.
- Digging underneath the player causes falling; placed blocks cannot intersect the player.
- F3 shows chunk/draw counters, player feet Y and grounded state in the window title.

## Regression checks

Default CTest covers physics, meshing, block edits and unload/reload. Assertions remain enabled in Release tests.
On a machine with a working OpenGL driver, enable the framebuffer check:

```powershell
cmake -S . -B build-cmake -DBLOXIDE_RENDER_TESTS=ON
cmake --build build-cmake --parallel 4
ctest --test-dir build-cmake --output-on-failure
build-cmake/Bloxide.exe --smoke-test
```

The hidden-window smoke test reads the framebuffer and fails if terrain pixels do not appear. It also prints draw calls, feet height and grounded state.

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

Benchmark results are written under `build/benchmark`: `v1.1-runtime.csv`, `v1.1-summary.json`, `v1.1-trace.json` and `v1.1-metrics.txt`. These are headless streaming measurements; the game does not export runtime timing files. F3 displays gameplay diagnostics in the window title.

## Controls

WASD moves, mouse looks, Space jumps, left mouse removes a breakable block, right mouse places grass, and Esc releases or captures the mouse.

## Project documents

- [ARCHITECTURES.md](ARCHITECTURES.md)
- [PERFORMANCE.md](PERFORMANCE.md)
- [CHANGELOG.md](CHANGELOG.md)
- [LICENSE](LICENSE)
