# Bloxide v1.1.0 performance report

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
