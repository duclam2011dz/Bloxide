# Changelog

## [1.5.0] - Unreleased

- Established first-party formatting/EditorConfig/Git attributes, contribution/development/style guides,
  ADRs, explicit historical documentation and the fixed seed/camera/resolution benchmark baseline.
- Moved the real entry point to main.cpp and introduced RAII GameApplication shutdown with worker
  joins before GL context destruction; shared --version/JSON build provenance across game/tools.
- Replaced the custom mini-loader with official pinned GLAD 2.0.8 and audited dependency/asset notices.
- Added architecture-gated scalar/SSE2/AVX2 builds, strict portable compiler flags, pinned vcpkg/SDL2,
  Ninja presets and distinct headless/render targets.
- Added UTF-8 executable/resource paths, writable user-data diagnostics, Windows/POSIX argv subprocesses
  and bounded timeout/exit handling; validated PNG I/O without narrow Windows filename assumptions.
- Added install/CPack ZIP/TGZ layouts, discovered runtime dependencies, relative Linux RPATH,
  package identity/checksums/notices and source-independent Unicode/read-only relocation checks.
- Added pinned-action Windows/Linux compiler matrices, mandatory Mesa/ASan+UBSan/TSan gates and
  experimental scalar-only macOS ARM64 CI. Actual qualification is recorded in PERFORMANCE.md.
- Preserved the existing worldgen versions/reference GCC output and all 112 material PNGs.
  No gameplay changes or disk persistence of edits/fluid journals were added.


## [1.4.0] - 2026-10-14

- Added density-world-1/schema-3 configuration, strong 3D overhang/cave terrain, climate fields and slope/depth-aware materials; preserved legacy world output/metadata.
- Added seeded iron/gold/diamond veins, water/aquifers, three tree species, flowers and deterministic bounded villages with houses, roads, foundations and simple interiors.
- Added uint16 BlockState, 112-layer material catalog, creative hotbar/picker, cutout mip coverage, translucent sorting, flowing/falling water at 10 Hz and swimming.
- Added dry, clear-AABB spawn search and frontier wakeup at unloaded water borders; long frames retain fluid tick debt.
- Replaced production heightmap far terrain with adaptive 3D indexed surfaces, constrained QEM, continuous morph safety/fine fallback, real XYZ morphing and GPU compaction.
- Fixed orphaned Meshing/Uploading states when an update group leaves the near region, allowing correct voxel re-entry after far ownership transfer.
- Added scalar/batch/SSE2/AVX2 noise paths, cached fractal kernels, reusable scratch, proven uniform-section skips, ore candidate batching and bit-exact backend qualification tools.
- Recorded benchmark build type/optimization accurately; only non-quick, five-repeat Release O2/O3 runs can qualify SIMD.
- Added shared queue credits/upload leases, worker batching, CPU/GPU payload budgets, job reservations and adaptive effective-distance recovery.
- Reserved headroom for the next far ring before distance recovery, preventing repeated admission/cancellation of an unaffordable band.
- Added pinned Dear ImGui DevTools, Windows capability-based telemetry, asynchronous per-pass GPU frame timings, inspection/lookup/teleport controls and bounded captures.
- Added unified profiler suites, recorded configurations, CSV/JSON/Chrome traces/screenshots, density reproducibility and resource/fluid/material/morph regressions.
- Kept metadata-only disk persistence; edits/fluid changes are RAM-only. Auto noise remains scalar pending deployment-specific SIMD qualification.

## [1.3.5] - 2026-10-12

- Added terrain-fields-1 with continental spline, roughness, mountain masks, ridges, valleys, plateau blending and flat/rolling-hills/mountainous presets.
- Added schema-2 configuration and --preset/--caves creation options while preserving schema-1/simplex-1 worlds without migration.
- Added height halos, section summaries, contiguous uniform fills, conservative cave skipping and in-job cancellation/revision checks.
- Added heightmap-only terrain to radius 24 using world-aligned 2/4/8-block LODs, hysteresis, stitched edges and voxel boundary closure.
- Added 250 ms interior geomorphing, shared publication transactions, handoff fallback meshes and nonblocking GPU retirement.
- Kept full voxels/caves/edits in radius 8; distant terrain represents only the original surface.
- Added GPU terrain timer queries, frustum culling, runtime CSV/JSON and terrain/LOD benchmarks.
- Fixed upload starvation caused by counting queue preparation against the 2 ms OpenGL budget.
- Added preset/reference-output, metadata, LOD/morph, streaming/reproducibility and OpenGL regressions.

## [1.3.0] - 2026-10-11

- Fixed side-contact upward snapping with swept face collision, constrained displacement and separate overlap recovery.
- Added int64 chunk/local-double player positions, floating camera/render origin and int64 targeting within +/-2^40 blocks.
- Preserved texture variants through rebasing with high/low 32-bit shader coordinate hashing.
- Added uint64 seeds, SplitMix64 derivation, stable signed coordinate hashing and immutable seeded simplex 2D/3D fields.
- Added normalized fBm, billow, turbulence, ridged feedback, domain warps and footprint-aware octave filtering.
- Replaced periodic terrain with seeded mountains/valleys and cached 3D caves that can reach the surface.
- Used full meshes throughout render distance to preserve cave mouths.
- Added CLI world selection/seed input, random-once default seed and atomically written metadata; edits remain RAM-only.
- Added noise/generator goldens, allocation/reproducibility tests, metadata regressions and render tests near both world limits.
- Added noise/generation/meshing benchmark and updated specs/performance evidence.

## [1.2.5] - 2026-10-11

- Added substepped ground/air acceleration, material friction, momentum-preserving jumping and weak air drag.
- Added deterministic C++ texture generation using periodic warping, fBM, cellular fields, procedural masks, palettes and ordered dithering.
- Exported 20 opaque 32x32 RGBA8 PNG variants, a manifest, contact sheet and mixed-variant repeat previews.
- Vendored pinned LodePNG with license; added strict PNG/catalog validation and executable-relative asset loading.
- Added a 20-layer Texture Array, full mip chain and nearest pixel-art filtering.
- Packed integer base-layer IDs into 32-bit vertices; repeated UV per block and selected variants in the shader without fragmenting greedy quads.
- Refreshed runtime assets/shaders on every build while keeping texture generation explicit.
- Added physics/material, palette/seam, loader rejection, layer/mip and six-face shader sampling regressions.
- Reviewed near/far render captures and updated specs/performance evidence.

## [1.2.0] - 2026-10-11

- Kept active CPU/GPU meshes visible throughout remeshing; added pending buffers and atomic update-group publication.
- Added instance/revision/group validation, overlapping-group merging, failed-upload retry and nonblocking fence retirement.
- Limited main-thread uploads to a soft 2 ms/frame budget; cleaned up unloaded GPU resources.
- Added shared DDA targeting at reach 6, black depth-tested outline and centered crosshair.
- Queued clicks against the previously presented target; blocked stale/locked targets, occupied placement and player overlap.
- Generated deterministic periodic hills without noise or seed, with three Dirt layers and protected Bedrock.
- Replaced flat LOD with coarse heightmaps, exact perimeter rings and full meshes for edited chunks.
- Added terrain/target/lifecycle regressions and delayed break/place framebuffer tests including outline occlusion.
- Added v1.2 CSV/JSON/trace benchmark output and updated specs/performance evidence.

## [1.1.5] - 2026-10-11

- Fixed sky-only rendering: packed vertex boundaries no longer wrap and horizontal face winding matches back-face culling.
- Aligned flat LOD surfaces at Y=64 and preserved edited terrain through full-detail meshes.
- Replaced fixed-height physics with gravity, player AABB, wall sliding, ceiling/floor collisions, bounded substeps and grounded jumps.
- Spawn now waits for terrain and finds a real support block; unloaded columns are collision barriers.
- Prevented placing blocks inside the player.
- Fixed remesh upload rejection, neighbor invalidation, edit snapshot ownership and journal indexing after reload.
- Destroyed GPU resources before deleting the SDL OpenGL context; handled minimized windows safely.
- Added physics/geometry/streaming regressions, opt-in framebuffer regression and Release-enabled assertions.
- Updated project specs and F3 diagnostics.

## [1.1.0] - 2026-09-28

- Replaced the fixed v1.0 world with infinite signed chunk coordinates.
- Added deterministic 16x256x16 chunk generation with protected Bedrock.
- Added generation/meshing worker threads, priority queues, cancellation and version checks.
- Added hidden-face removal, greedy meshing, packed vertices, index buffers and LOD1.
- Added in-memory block edit journal and remesh-only edit path.
- Added chunk benchmark executable with CSV, JSON and Chrome trace output.
- Added updated architecture, benchmark and streaming documentation.

## [1.0.0] - 2026-09-27

- Added SDL2/OpenGL 3.3 Core application window.
- Added vendored GL loader, shader renderer and visible voxel faces.
- Added flat grass, dirt and stone world.
- Added FPS camera, movement, jumping and block interaction.
- Added CMake build and headless world smoke test.
- Added project architecture and setup documentation.
