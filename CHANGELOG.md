# Changelog

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
