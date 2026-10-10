# Bloxide performance and validation

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
