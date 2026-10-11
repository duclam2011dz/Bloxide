# Bloxide performance and validation

## v1.3.0 - 2026-10-11

Environment: Windows, MSYS2 UCRT64 GCC 16.1, Intel Core i5-6400 @ 2.70 GHz (4 cores/4 threads), NVIDIA GeForce GT 720, driver 27.21.14.6192. Measurements are local observations, not portable performance guarantees.

### Correctness and reproducibility

Final Debug CTest passed 10/10 in 27.72 s; Release passed 10/10 in 10.62 s. These include six-face swept collision, the side-penetration/no-lift regression, overlap recovery, floating-origin targeting, metadata validation, caves, and OpenGL regressions near both gameplay limits. Separate processes, reversed chunk order and different worker schedules produced identical block hashes for seeds 0, 130 and 18446744073709551615. Debug and Release probe outputs also matched exactly.

Simplex golden values use tight numerical tolerances; seed-130 chunk (0,0) has pinned block digest 2061680697937486093 for simplex-1. Allocation instrumentation observed zero allocations during valid noise sampling. The normalization grid observed noise values from -0.996569 to 0.997312. A 25-chunk seed-130 scan found 23,963 carved cave blocks and 156 surface openings. These are fixture results, not global terrain statistics.

### Release headless benchmark

Run BloxideNoiseBenchmark from the build directory; CSV output is build/benchmark/v1.3-noise.csv (ignored by Git). Sampling uses one million samples. Generation/meshing averages below use four sampled chunks. Streaming uses the production worker pipeline with simulated upload acknowledgments and 2 ms driver pacing.

| Metric | Observed value |
|---|---:|
| Simplex 2D | 43.0282 ns/sample |
| Simplex 3D | 66.0126 ns/sample |
| fBm, footprint 1 | 261.157 ns/sample |
| fBm, footprint 4 | 182.168 ns/sample |
| fBm, footprint 16 | 93.162 ns/sample |
| Generation average | 1.02522 ms/chunk |
| Meshing average | 4.72717 ms/chunk |
| Four sampled mesh payloads | 116,120 bytes |
| Cold stream, 441 generated / 289 active | 644.520 ms |
| Traversal settle | 171.885 ms |
| Edit to publish | 15.5233 ms |
| Maximum driver iteration | 34.4539 ms |
| Loaded chunks after traversal | 483 |
| Active CPU mesh payload | 8,416,680 bytes |
| Stale results in this run | 0 |

483 block arrays alone occupy 31,653,888 bytes. This excludes map/pool overhead, pending snapshots, worker caches, retired resources and GPU storage; the mesh value likewise measures payload, not process RSS. Headless timings are not FPS, GPU time or hardware utilization. Cold/traversal timings include scheduling and polling, not only noise evaluation. Earlier flat/periodic-world reports below are historical references, not equivalent workloads for a speedup claim.

### OpenGL and visual regression

Final Release fixtures at X/Z offsets +1099511627648 and -1099511627648 rendered 289 draws and approximately 921,600 terrain pixels. Old meshes remained visible for all 15 deliberately delayed remesh frames. Group publication counted 291 swaps and zero stale results; edit-to-publish was 126.506 / 125.764 ms. The fixture intentionally injects an 80 ms worker delay, framebuffer readbacks and screenshots; its maximum diagnostic frame times (278.048 / 259.013 ms) are not normal gameplay frame performance.

Texture loading measured 5.5209 / 5.6748 ms and texture upload CPU submission 0.136 / 0.162 ms in those runs. This is not GPU completion time. The near/far captures were reviewed for terrain, material mapping and retained texture detail; the fixtures also check outline occlusion and stable high/low-word texture hashing after rebasing.

Gameplay is restricted to +/-2^40 blocks. Metadata persists seed/configuration/version only; edits remain RAM-only. Full meshes replace heightmap LOD to preserve cave mouths, so cave-heavy workloads may cost more memory and meshing time. No claim is made about vendor fragment/geometry utilization.

## v1.2.5 - 2026-10-11

Debug and Release each passed 6/6 tests, including the optional OpenGL framebuffer regression. The v1.2.5 asset/physics test and framebuffer test additionally passed three consecutive Release repetitions.

### Texture pipeline measurements

Environment remains local Windows/MSYS2 UCRT64, GCC 16.1 and OpenGL 3.3. The explicit generator run with --seed 125 reproduced all PNG SHA-256 hashes byte-for-byte.

| Metric | Observed value |
|---|---:|
| Material assets | 20 x 32x32 source RGBA8 |
| Generator/export time, including previews | 129.412 ms |
| Generator-side strict catalog load | 6.8702 ms |
| Total material PNG bytes | 17,083 |
| All PNG bytes including two previews | 91,908 |
| Renderer load, final repeated Release run | 5.9352 ms |
| Array upload/mipmap CPU submission time | 0.1137 ms |
| Base-level RGBA8 payload | 81,920 bytes |
| Six-level mip payload | 109,200 bytes |

The upload time measures CPU API submission, including glGenerateMipmap; it does not measure GPU completion or actual driver VRAM overhead. Generation includes PNG encoding and the contact/tiled sheets. Load timings vary with filesystem cache.

### Visual and correctness checks

The source contact sheet and mixed-variant 3x3 previews were inspected, followed by near/far in-game captures after 289 chunks became active. Captures are generated under the executable's diagnostics directory. Pixel-art remains crisp near the player, material mapping is distinct and mipmaps soften distant detail.

The OpenGL test validates all 20 source layers by readback and checks six mip dimensions. It then compares shader samples on 8x4 quads for every face, each base material, positive/negative world coordinates and AO levels 0/3. Sampling checks cover UV repetition, CPU/GLSL variant hash agreement, side orientation and integer layer decoding.

The final repeated Release render run retained old meshes across 15 delayed-edit frames, produced 1,212 visible black outline pixels, and ended with 289 draw calls and player grounded at feet Y=74. Its maximum diagnostic frame was 259.791 ms; this includes framebuffer readbacks and encoding two 1280x720 PNG screenshots, so it is not a gameplay frame-time estimate.

Runtime was also launched from build-tools rather than the repository/executable directory. It resolved assets successfully. Temporarily removing the generated runtime copy of stone_0.png caused startup to return 1 and name the missing file; the file was restored afterward.

CPU tests verify deterministic seeds/variants, palette membership, opposite-edge wrapping, cross-variant borders, manifest ordering/duplicates, PNG corruption/size/RGB/indexed/16-bit/alpha rejection, mixed-material braking, acceleration/air drag, diagonal speed, jump momentum and 30/60/144 FPS consistency. The terrain generator and its lack of noise/seed remain unchanged.

## v1.2.0 - 2026-10-11

Environment: local Windows/MSYS2 UCRT64, GCC 16.1, CMake 4.4.3, SDL2 and an OpenGL 3.3 context. Debug and Release each passed 5/5 CTest checks. The lifecycle and framebuffer regressions additionally passed three consecutive Release repetitions.

### Headless lifecycle benchmark (Release)

Run from the repository root with build-cmake/BloxideBenchmark.exe. Cold streaming waits for 289 active chunks, followed by steady-state, traversal and ten alternating break/place edits. CPU upload acknowledgement is simulated; worker generation/meshing still executes asynchronously.

| Metric | Observed value |
|---|---:|
| Driver samples | 419 |
| CPU driver P95 | 0.1466 ms |
| CPU driver maximum | 0.5732 ms |
| Edit-to-publish P95 | 16.3849 ms |
| Edit-to-publish maximum | 18.7382 ms |
| Loaded / active chunks | 483 / 357 |
| Published groups | 371 |
| Stale results in this traversal | 0 |
| Trace events | 419 |

CSV: build/benchmark/v1.2-streaming.csv; JSON: v1.2-summary.json; Chrome/Perfetto trace: v1.2-trace.json. The trace records actual CPU driver sample timestamps/durations. These timings exclude the 2 ms pacing sleep and do not measure worker execution time, OpenGL upload time, GPU utilization or gameplay FPS.

### OpenGL delayed-edit regression (Release, final repeated run)

The hidden-window test deliberately delays each meshing job by 80 ms and rejects one upload for the edited chunk in both break and place. It checks terrain pixels every frame, visible/occluded outlines, crosshair, active mesh retention, group publication and complete fence retirement.

| Metric | Observed value |
|---|---:|
| Terrain pixels after edits | 887,004 |
| Visible black outline pixels | 1,212 |
| Old-mesh frames retained across two edits | 22 |
| Break-to-publish under injected delay | 212.900 ms |
| Place-to-publish under injected delay | 178.297 ms |
| Maximum diagnostic frame | 89.006 ms |
| Draw calls / GPU group swaps at completion | 27 / 29 |
| Player feet / grounded | Y=74 / true |

The diagnostic frame maximum includes framebuffer readbacks, pixel analysis, startup work and the scripted scenario. It is not a gameplay frame-time estimate. Injected delay explains the large publication latency; old geometry remains visible during it. Chunk/draw counts depend on worker completion order.

The CPU lifecycle test separately injects a meshing failure, retains both active border meshes through partial upload, merges an overlapping edit, rejects stale tokens, changes LOD and unloads/reloads instances. Framebuffer checks wait for initial terrain before testing retention, avoiding a false failure when far chunks finish before the spawn chunk.

### Historical reports

The v1.1 results below used a flat world and different benchmark procedures, so they are not an apples-to-apples performance baseline for periodic terrain.

## v1.1.5 correctness validation — 2026-10-11

This patch targets correctness rather than new performance optimization. Debug and Release CTest each passed all four tests with the optional OpenGL framebuffer check enabled. A direct framebuffer smoke run detected 653,702 green terrain pixels and 39 draw calls; player feet were Y=64 with grounded=1. Counts depend on asynchronous streaming and are not performance thresholds.

The previous process-liveness smoke check did not verify visible geometry. The new regression checks vertex boundaries, outward triangle normals, collision outcomes and actual framebuffer content. The older headless timings below measure streaming scheduling; they do not establish runtime FPS or GPU performance.

## Method

The benchmark was run on 2026-09-28 using the local Windows/MSYS2 UCRT64 GCC 16.1 toolchain. `BloxideBenchmark` uses four scenarios: cold stream, steady-state, traversal and edit/remesh positions. It records frame and generation phase timings in CSV and writes a JSON summary plus Chrome trace.

The benchmark is headless, so `visible=0` is expected: no OpenGL upload/render context is created. Runtime GPU utilization is not inferred from the headless data. A GPU timer-query pass should be collected on a supported driver when comparing render behavior.

## Observed run

| Metric | Value |
|---|---:|
| Sampled frames | 1,017 |
| Average frame time | 0.0862 ms |
| P95 frame time | 0.1207 ms |
| Average streaming/generation phase | 0.0794 ms |
| Final loaded chunks | 509 |
| Generation jobs | 833 |
| Meshing jobs | 545 |
| Cancelled jobs | 0 |

The result is a CPU scheduling/generation measurement, not a game FPS claim. The output files are generated under `build/benchmark` and are intentionally ignored by Git.

## Reproduce

```powershell
build-cmake/BloxideBenchmark.exe
build-cmake/BloxideBenchmark.exe --quick
```

Use `v1.1-runtime.csv` for frame distributions and `v1.1-trace.json` with Chrome tracing tools for phase timelines. Compare reports on the same machine and build configuration; no hardware-independent FPS threshold is used.
