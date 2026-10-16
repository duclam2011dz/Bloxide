# Bloxide performance and validation

## v1.5.0 portability qualification (in progress)

This section is not a relabeling of v1.4 measurements below. Portable-build/release gates
must identify the exact tested commit, provider, compiler/OS/architecture and package.

Development smoke on Windows UCRT64 GCC 16.1 with the system SDL2 provider passed 12/12
headless tests (27.74 s) and 5/5 real-driver GPU tests (123.05 s), including the six-move/reversal
LOD fixture. The relocated ZIP passed strict asset validation (missing/corrupt rejection),
Unicode executable paths, a different cwd, a write-denied install prefix, native child profiler
and hardware GL smoke. These were dirty development builds, not final tagged packages.

Scalar-only compiled without either x86 SIMD translation unit, passed subprocess/capability tests
and retained density-world-1 seed-130 hashes. All three generator probe sets exactly match the
v1.4 reference in benchmarks/baselines/v1.4-reference-hashes.txt. No PNG was regenerated.

BloxideProfiler --baseline --quick completed all four steady near/LOD x overlay variants.
Its camera metadata was [8.5,160,72.5], yaw/pitch [-90,-25], framebuffer [1280,720],
budgets 512/128 MiB and canonical world-config fingerprint 31358b68611bb560.
These 2 s smoke windows are explicitly NOT qualified performance windows; do not use their FPS
as the five-repeat hardware baseline. Full windows are 5 s warmup, 30 s samples, five repeats.

CI and canonical pinned-vcpkg qualification remain pending at this point. The release tag must
not be created until mandatory clean-runner, sanitizer, Mesa and relocated-package gates pass.


## v1.4.0 - 2026-10-14

### Environment, scope and methodology

Final builds use Windows/MSYS2 UCRT64, GCC 16.1.0, strict floating-point operations (no fast-math/FMA contraction), C++17 and OpenGL 3.3. Host: Intel Core i5-6400 @ 2.70 GHz, four cores/threads, 4,251,672,576 bytes physical RAM; NVIDIA GeForce GT 720, OpenGL 3.3.0 / NVIDIA 461.92. These are local observations, not portable FPS guarantees.

Noise comparisons use seed 130, identical signed coordinate grids, five non-quick repeats, and scalar/batch/SSE2/AVX2 sample and generated-block equivalence checks. Qualification requires a Release O2/O3 build, >=10% batch throughput improvement and no >5% generation p95/p99 regression. Debug/quick runs cannot qualify SIMD. Production Auto remains scalar; explicit ISA paths are diagnostics until qualified for a deployment. O2/O3 are separate clean Release builds, not a project-wide native ISA switch. Tool exports record build type and configured optimization (Debug 0, Release 2/3, other configurations unknown -1); custom user compiler flags are outside that label.

Worldgen microbenchmarks compare reference/optimized generation of exactly the same density-world-1 block arrays, then measure meshing and constrained hierarchy construction separately. Runtime benchmarks use a hidden real 1280x720 OpenGL window, VSync off, fixed seed/configuration and spectator camera. Frame times are start-to-start, including driver work and previous-frame instrumentation; phase timings describe the current frame. Delayed GPU results carry their original frame IDs. Screenshots/final export happen after the sample window.

The requested far radius is 24, not a guarantee that every scene fits it. Nominal steps 2/4/8 retain finer geometry when topology/material/morph safety cannot be proved. Effective distance is budget-driven. LOD on/off changes visible extent; it is not an equivalent-work speedup comparison. Forest/village fixtures explicitly use flat, caves-off, water-off metadata to isolate features. Swimming/collision tests exercise physical movement independently of the spectator benchmark.

### Final Release noise measurements

Median values over five repeats, seed 130, identical batch grids:

| Build/backend | 2D ns/sample | 3D ns/sample | generation p95 / p99 ms | qualification |
|---|---:|---:|---:|---|
| O3 scalar | 41.1812 | 65.9022 | 5.1034 / 5.3180 | baseline |
| O3 SSE2 | 30.3543 | 92.0849 | 6.0635 / 6.2510 | rejected |
| O3 AVX2 | 21.9825 | 53.7967 | 4.3553 / 4.5588 | both batch gates passed |
| O2 scalar | 45.7869 | 72.6457 | 5.9972 / 6.1553 | baseline |
| O2 SSE2 | 46.5319 | 76.2311 | 6.2654 / 6.3589 | rejected |
| O2 AVX2 | 34.1786 | 57.0727 | 5.2179 / 5.3075 | both batch gates passed |

O3 scalar throughput is approximately 24.28 million 2D / 15.17 million 3D samples/s; AVX2 reaches 45.49 / 18.59 million. SSE2's O3 2D improvement does not offset its 3D/generation regression, so it remains unqualified. All measured backends produced exact scalar samples and block hashes. The tool records per-repeat samples/s and latency in CSV rather than averaging unlike workloads. Clean O2 also passed all 11 enabled headless CTest tests in 23.22 s; OpenGL regressions were run on O3 and Debug.

Artifacts: build/benchmark/v140-release-final-O3 and v140-release-final-O2. These numbers describe this machine/toolchain and the tested workload; Auto is not globally changed based on one machine.

### Correctness and release regression evidence

Clean Release O3 passed 16/16 CTest tests in 134.30 s; clean Debug passed 16/16 in 260.09 s. Driver-backed tests include Texture Array/mips, six-face UV/material/AO sampling, cutout/translucent depth behavior, XYZ morph/depth fixtures, input capture/resize, outline occlusion, injected upload failure, delayed remesh, retirement, six near/far moves/reversals and floating origin near both signs of the world limit.

The Release density LOD fixture requests radius 24 and finished in 97.93 s (1,232 far tiles at the final adaptive radius 19). Debug requests radius 12 and finished in 106.46 s. Headless LOD integration still covers all bands/full radius 24. Neither smoke test is an FPS benchmark: readbacks, artificial 80 ms mesh delay and failure injection are intentionally expensive.

Release delayed-remesh LOD evidence: the old chunk stayed visible for 17 frames; break/place publication took 219.774/118.778 ms; 35 stale results were discarded, with six completed reversals. The cold/traversal diagnostic maximum frame was 819.898 ms, not a steady-state percentile or a hidden success threshold. First texture load in a cold process was 177.285 ms; a warm LOD process loaded in 23.990 ms and submitted upload in 1.031 ms. Upload submission is CPU duration, not GPU completion time.

Legacy seed-130 chunk goldens remain 2061680697937486093 (simplex-1) and 15604160045169882075 (terrain-fields-1). Reproducibility tests cover seeds 0/130/uint64-max across separate processes, reverse orders and threaded probes. Final direct probes additionally matched Debug/O2/O3 exactly for all three generator versions and all three seeds. Metadata tests preserve legacy files, reject conflicts and round-trip schema 3. Core regressions cover density normals and filtered scalar/batch grids, optimized/reference sections, ore/tree/village stamping, fluid flow/source conversion/drainage/resume, FPS-independent ticks, swimming, queue caps, retry/cancel/re-entry and budget recovery.

### Worldgen, fluid and actual runtime measurements

Five worldgen repeats / 160 chunks, seed 130, scalar O3; reference and optimized arrays matched exactly:

| CPU phase | p50 ms | p95 ms | p99 ms |
|---|---:|---:|---:|
| Reference generation | 7.3835 | 7.9283 | 9.7106 |
| Optimized generation | 4.2787 | 4.5833 | 4.7281 |
| Greedy meshing | 7.1555 | 7.4856 | 7.5966 |
| Constrained simplification/mapping | 2.1282 | 25.9988 | 44.6181 |

The optimized median is about 42% lower in this same-input test. Hierarchy tail latency remains significant on intricate topology: safety checks preserve geometry rather than forcing an unsafe coarse result. Maximum standalone hierarchy payload was 337,176 bytes; neighbors/reference generation are not included in the timed meshing phase.

Five fluid runs / 1,500 ticks measured p50/p95/p99 of 0.0001/0.0078/0.0506 ms, 885 updates and maximum frontier 128. This fixture includes many idle ticks after settling, so its median is not the cost of a large active waterfall. The raw CSV allows active-frontier-only analysis; these headless timings are not FPS.

Actual OpenGL steady runs below each have 5 s warmup and 30 s sampling after near/far readiness. Same density-world-1 seed 130 / mountainous, scalar O3, fixed camera:

| Scene | FPS | frame p50 / p95 / p99 ms | effective far radius | GPU mesh bytes |
|---|---:|---:|---:|---:|
| LOD on, DevTools off | 51.592 | 18.7976 / 22.0715 / 28.4591 | 19 | 78,512,944 |
| LOD on, DevTools on | 52.226 | 19.0101 / 20.2914 / 22.3953 | 19 | 78,512,944 |
| LOD off, DevTools off | 106.673 | 9.4087 / 10.4543 / 11.0624 | 8 | 4,511,520 |

These are one sequential run per scene, not five-run confidence intervals. Small UI on/off differences are run variability, not evidence that the overlay speeds rendering up. The no-LOD case renders a much smaller region. No frame samples were dropped.

At the end of the LOD-on/DevTools-off run, tracked CPU cache was 253,996,756 bytes; working set/private commit 450,220,032/566,259,712 bytes; mapped DXGI local process usage 125,550,592 bytes. Latest available GPU opaque/cutout/translucent timings were 5.660/1.708/2.390 ms (total terrain 9.758 ms); they are individual delayed observations, not average utilization. The overlay-on run had identical payloads and a latest terrain observation of 12.001 ms. The near-only run tracked 64,826,696 CPU bytes. GPU-engine hardware utilization stayed N/A.

Mean CPU phase durations from the sampled CSV tail, milliseconds (elapsed submission may include driver waits):

| Phase | LOD / UI off | LOD / UI on | near only |
|---|---:|---:|---:|
| World driver | 0.5770 | 0.4965 | 0.1382 |
| Upload submission | 0.0078 | 0.0073 | 0.0002 |
| Rendering submission | 13.2808 | 12.5377 | 4.2252 |
| Fluid driver | 0.1764 | 0.1611 | 0.1430 |
| UI | 0.1103 | 1.0018 | 0.0808 |
| Draw calls/frame | 2,567 | 2,567 | 931 |

Rendering submission/transparent ordering and conservative hierarchy tails remain useful optimization targets. These phases do not add up to the entire frame (culling, queue preparation, swaps, telemetry and CSV overhead remain), and percentages of elapsed phases would not be hardware CPU utilization.

The final unified quick suite completed all 20 real-window cases: cold, steady, traversal, reversal, fluid, edits, budget pressure, forest, village and underwater, each with overlay off/on, plus noise/worldgen/fluid microbenchmarks. All 23 summary JSONs parsed successfully and exported Chrome trace durations were nonnegative. Captures were visually reviewed for terrain/water, village/forest, hotbar and DevTools. Artifact root: build/benchmark/v140-release-suite. Quick runtime windows are 2 s with 0.5 s warmup (cold uses no warmup); they validate tooling/scenarios, not long-run stability or performance confidence.

Artifacts: build/benchmark/v140-release-worldgen, v140-release-fluid, v140-release-lod-plain, v140-release-lod-ui and v140-release-near-plain. Raw frame CSVs include cold load and warmup; the last runtime-summary frame count selects the sampled tail. Capture summaries instead describe their explicitly labeled rolling history and must not be substituted for the fixed-window runtime summary.

### Assets and memory accounting

The catalog has 112 material PNGs: 28 groups x four variants (the estimate of 108 grew by the Cobblestone group). The original 20-layer prefix is unchanged. Material PNGs total 84,982 compressed bytes; base RGBA payload is 458,752 bytes and all six mip levels total 611,520 bytes. Two contact/repeat preview PNGs are development assets, not texture layers.

CPU cache defaults to 512 MiB and GPU mesh payload to 128 MiB. Accounting includes active/pending/previous/completed meshes, borrowed snapshots and worker reservations. These are payload/admission budgets, not process RAM or driver VRAM limits. Authoritative edit/fluid journal cell payload is separate, is never evicted, and excludes map/allocator overhead; warnings start at 128 MiB. Distance recovery reserves room for a new ring plus a far job to avoid repeated admission/cancellation.

### Telemetry and measurement limits

Windows CPU/RAM telemetry is sampled at 1 Hz. Worker elapsed sums are neither utilization percentages nor serial frame time. GPU opaque/cutout/translucent passes use an asynchronous query ring; the total command interval can include CPU/driver scheduling and GPU idle gaps, so it is not a hardware-busy metric. On this host adapter-mapped DXGI memory is available; matched GPU-engine utilization is N/A. Fragment/geometry/texture-sampling hardware utilization is not claimed.

History is bounded at 4,096 DevTools frames, 65,536 trace events and 1,000,000 runtime frame samples. Capture queues have finite depth/byte limits. Exports report overwritten/dropped data and incomplete histories; JSON preserves timestamp precision. A quick suite is smoke coverage, not a five-repeat performance confidence interval. RAM-only edits/fluid changes are lost on exit. No benchmark writes to the user's default world.

Raw CSV/JSON/Chrome traces, machine/world configuration and screenshots are generated under build/benchmark and ignored by Git. Reproduce using the unified commands in README; full runtime defaults are five repeats, 5 s warmup and 30 s sampling per scenario, and can take a long time.

## v1.3.5 - 2026-10-12

Environment: Windows/MSYS2 UCRT64, GCC 16.1, Intel Core i5-6400 @ 2.70 GHz (4 cores/4 threads), NVIDIA GeForce GT 720, driver 27.21.14.6192. Values below are local observations, not portable FPS guarantees.

### Methodology and correctness

Before refactoring, a v1.3.0 Release quick baseline at seed 130 measured generation 1.29102 ms/chunk, meshing 5.29347 ms/chunk, cold streaming 692.284 ms and edit publication 18.0612 ms. The new height fields change the terrain workload, so these are historical reference values, not an equivalent-work speedup comparison.

The new full TerrainBenchmark uses seed 130, 100,000 height samples, 32 generation/standalone-mesh tiles, 120 steady driver samples, and six settled chunk-center moves/reversals. Optimized/reference generation shares the same configuration and coordinates. Streaming runs compare far LOD enabled/disabled on the same terrain-fields-1 world; enabling LOD extends the visual radius from 8 to 24, so those runs intentionally render different extents. Headless upload acknowledgements are simulated.

Simplex-1 keeps its seed-130 chunk-(0,0) digest 2061680697937486093. Terrain-fields-1 pins 15604160045169882075. Separate processes, reversed order and threaded probes match exactly for seeds 0, 130 and uint64 max; Debug and Release outputs match for both generators. Optimized/reference block arrays match across all presets, cave modes, negative coordinates and near-limit coordinates. Metadata tests verify that opening a legacy world does not rewrite its file.

### Release measurements

| Metric | Observed |
|---|---:|
| Shared terrain height sampling | 1,772.95 ns/sample |
| Reference generation | 1.52302 ms/chunk |
| Optimized generation | 1.29369 ms/chunk |
| Heightmap step 2 / 4 / 8 | 0.821525 / 0.414397 / 0.355922 ms/tile |
| Standalone tile payload, step 2 / 4 / 8 | 19,584 / 12,096 / 10,368 bytes |
| Standalone triangles, step 2 / 4 / 8 | 416 / 240 / 200 |

Optimized generation was approximately 15% faster than reference in this run. This is a single sequential local measurement, not a statistical guarantee; cache and scheduling effects can change results. Standalone tiles include all four fallback skirts; production tiles generally need fewer.

| Headless pipeline metric | Far LOD off | Far LOD on |
|---|---:|---:|
| Cold stream | 649.332 ms | 726.977 ms |
| Active CPU mesh payload | 7,608,800 bytes | 18,224,700 bytes |
| Triangle count | 380,440 | 644,118 |
| Far vertices | 0 | 310,492 |
| Voxel records at cold completion | 441 | 441 |
| Generation worker elapsed sum | 599.845 ms | 623.097 ms |
| Meshing worker elapsed sum | 1,245.09 ms | 1,376.68 ms |
| Heightmap worker elapsed sum | 0 ms | 611.059 ms |
| Steady driver p50 / p95 / p99 | 0.0431 / 0.0456 / 0.0480 ms | 0.2282 / 0.2508 / 0.2998 ms |
| Six settled moves/reversals | 154.796 ms | 417.815 ms |
| Edit to publish | 15.8315 ms | 15.3049 ms |
| Maximum driver iteration | 7.507 ms | 32.2328 ms |
| Stale voxel / heightmap results | 0 / 0 | 0 / 1 |

Worker elapsed sums overlap across threads and can include scheduling/lock delay; they are not process CPU utilization and must not be added to wall time. Driver percentiles measure main-thread headless orchestration, not gameplay frames. Payload excludes capacities, maps, snapshots, pools, pending meshes, retired buffers and GPU overhead. 441 block arrays alone occupy 28,901,376 bytes; halo/section metadata adds 680 bytes per chunk before alignment and other fields.

Profiling an earlier implementation found approximately 1.16 ms steady driver p50 with far LOD: it rescanned all distant tiles each frame. Processing results while rebuilding requests only when center/border revision changes reduced this to 0.2282 ms in the final run. The earlier short run is indicative, not a controlled statistical comparison.

### OpenGL diagnostics and visual review

The real OpenGL LOD regression waits for 2,112 far candidates, performs six chunk-center moves/reversals, checks CPU/GPU representation tokens every frame, then runs delayed break/place and buffer-retirement checks. A separate raster fixture validates integer heightmap attributes and morph endpoints. Full-resolution shared edges, winding and voxel wedge closure also have geometry tests.

The standalone Release traversal diagnostic completed with 2,112 far candidates, 144 terrain draws after frustum culling, 14 old-mesh remesh frames, six reversals and zero stale voxel results. Artificially delayed edit publication was 126.314 ms; the latest asynchronously available GPU terrain pass was 1.80429 ms. Maximum diagnostic frame time was 637.238 ms. These numbers include cold startup, screenshots, readbacks, VSync and an injected 80 ms meshing delay; they are not steady gameplay FPS.

Runtime CSV/JSON exports CPU frame/phase timings, cumulative worker work and asynchronous GPU pass time under the executable's benchmark directory. Far candidate count precedes renderer frustum culling and is not actual draw visibility. GPU elapsed queries do not expose shader-stage hardware utilization or driver VRAM residency. The query ring and retirement fences are polled without glFinish or a blocking result wait.

Near and elevated LOD overview captures were reviewed for terrain shape, texture repetition and transitions. Distant heightmaps deliberately omit caves and edits; full voxel detail returns inside radius 8. Skirts/wedge closure hide transitions but do not make distant terrain a faithful cave or edited-world representation.

### Regression discoveries

Testing caught and fixed two-direction CPU/GPU ownership retention during handoff, inverted closure/skirt winding, and Debug upload starvation: queue sorting previously consumed the entire 2 ms budget before any OpenGL call. Ownership/reversal tests additionally passed three consecutive Release runs.

Final Release CTest passed 13/13 in 11.97 s; Debug passed 13/13 in 41.09 s. Both include actual OpenGL traversal/reversal, morph rasterization, delayed edit swapping, outline occlusion and near-limit rendering. Strict CPU/GPU mesh-token checks run throughout the smoke frames. The probe comparison covers both generator versions at all three seeds on the supported toolchain.

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
