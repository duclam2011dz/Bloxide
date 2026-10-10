# Changelog

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
