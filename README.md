# Bloxide

Bloxide v1.2.5 is a C++17 voxel game prototype using SDL2 and OpenGL 3.3 Core.

## v1.2.5 physics and textures

Movement accelerates toward 5 blocks/s at 45 blocks/s^2 on ground and 12 blocks/s^2 in air. Releasing input applies exponential material friction: Grass 14, Dirt 12, Bedrock 11 and Stone 9 s^-1. Mixed support uses the strongest friction. Air drag is 0.15 s^-1 and jumping preserves horizontal momentum. Collision remains substepped at at most 5 ms.

Blocks now use 20 procedural PNGs (32x32 RGBA8) in a single Texture Array. Five material groups each have four variants: Grass top, Grass side, Dirt, Stone and Bedrock. Grass bottom shares Dirt. Integer layer IDs, coordinate-hashed variants and block-space UV repetition keep greedy quads large without stretching their textures.

The C++ generator combines tileable domain warping, cellular/Voronoi fields, four-octave fBM, material masks, palette quantization and Bayer dithering. Texture seed defaults to 125 and does not affect the terrain generator. See [texture contact sheet](assets/textures/contact_sheet.png) and [3x3 repeat previews](assets/textures/tiled_preview.png).

```powershell
cmake --build build-cmake --target BloxideTextureGenerator
./build-cmake/BloxideTextureGenerator.exe --output assets/textures --seed 125
cmake --build build-cmake --parallel 4
```

PNG assets and their manifest are committed, so normal builds do not run generation. Every build refreshes assets/shaders beside the executable; runtime resolves them from the executable directory and works from unrelated working directories. Missing/corrupt files, incorrect dimensions, RGB/indexed/16-bit PNGs or nonopaque alpha stop renderer startup with a filename-specific error. LodePNG is vendored at a pinned commit with its zlib license.

Texture filtering is nearest up close with nearest mip samples blended between levels at distance. Material tiles wrap both axes except the deliberately directional Grass side, which wraps horizontally and has grass above dirt.

## Features

- Infinite streamed chunks sized 16x256x16 (XxYxZ), with block Y=0..255.
- Generation radius 10, render radius 8, and configured simulation radius 6 (no block tick system yet).
- Four CPU workers: two generation workers and two meshing workers.
- Hidden-face removal, greedy meshes, packed 32-bit vertices, indexed drawing and depth/back-face culling.
- Active/pending CPU and GPU meshes: edits keep the old mesh visible until the replacement group is fully uploaded.
- Border edits publish affected chunk meshes together. Overlapping edits merge groups; obsolete job results cannot replace newer revisions.
- Soft 2 ms/frame upload budget and nonblocking GPU-fence retirement.
- Black depth-tested block outline, centered crosshair and one shared target for both rendering and interaction.
- Reach is 6 blocks from the eye to the hit face. Bedrock is targetable but unbreakable.
- Player AABB, gravity, grounded jumps, walls/ceilings, and terrain-based spawn.

## Terrain

For positive modulo x/z in 0..127:

```text
H = floor(64 + 10*sin(2*pi*x/128) + 6*cos(2*pi*z/128))
```

Height ranges approximately 48..80 and repeats every 128 blocks on each axis. There is no noise, seed or randomness. Each column contains Bedrock at Y=0, Stone at Y=1..H-4, three Dirt layers, Grass at Y=H and Air above.

LOD0 covers chunk radius 4. LOD1 at radius 5..8 uses maximum-height interior patches up to 4x4 while preserving a one-block exact perimeter. Vertical step faces close the coarse geometry. Edited chunks use full geometry; LOD transitions use the same swapping pipeline.

## Build and run

Windows/MSYS2 UCRT64 requires GCC, CMake and SDL2. If the MSYS2 CMake binary fails, use official Windows CMake (the local portable copy is under build-tools).

```powershell
cmake -S . -B build-cmake -G "MinGW Makefiles" -DCMAKE_C_COMPILER=D:/msys64/ucrt64/bin/gcc.exe -DCMAKE_CXX_COMPILER=D:/msys64/ucrt64/bin/g++.exe -DCMAKE_MAKE_PROGRAM=D:/msys64/ucrt64/bin/mingw32-make.exe -DCMAKE_PREFIX_PATH=D:/msys64/ucrt64 -DCMAKE_BUILD_TYPE=Release
cmake --build build-cmake --parallel 4
ctest --test-dir build-cmake --output-on-failure
./build-cmake/Bloxide.exe
```

## Controls and edit behavior

WASD moves, mouse looks, Space jumps, left click breaks the outlined block, right click places Grass next to its hit face, Esc releases/captures the mouse, and F3 toggles window-title diagnostics.

Click validates the outlined target from the previously presented frame. Changed camera target, expired revision, out-of-reach target or a locked chunk causes the click to be ignored. Placement requires an empty loaded cell that does not overlap the player. During remesh, collision follows the edit immediately while targeting is temporarily locked on the affected group.

## Tests and benchmark

The CPU suite covers terrain periodicity/layers, DDA targeting, acceleration/material friction, PNG validation, procedural palette/seam checks, journal reload, mesh revisions, overlapping group swaps and LOD transitions. Assertions remain enabled in Release tests.

```powershell
cmake -S . -B build-cmake -DBLOXIDE_RENDER_TESTS=ON
cmake --build build-cmake --parallel 4
ctest --test-dir build-cmake --output-on-failure
./build-cmake/Bloxide.exe --smoke-test
./build-cmake/BloxideBenchmark.exe
```

The optional hidden-window OpenGL test checks every texture layer and mip dimension, then compares repeated shader samples on large quads for six faces, signed coordinates and AO. It also checks terrain/outline pixels, outline occlusion, crosshair, delayed remesh, failed-upload retry and retired buffers. Near/far review PNGs are saved in the executable's diagnostics directory during the test.

The benchmark writes build/benchmark/v1.2-streaming.csv, v1.2-summary.json and v1.2-trace.json. It measures CPU driver work and edit-to-publish latency with simulated upload acknowledgement; it is not a GPU or gameplay-FPS measurement. Use --quick for a short run.

See [ARCHITECTURES.md](ARCHITECTURES.md), [PERFORMANCE.md](PERFORMANCE.md), [CHANGELOG.md](CHANGELOG.md) and [LICENSE](LICENSE).
