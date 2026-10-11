# Bloxide

Bloxide v1.3.0 is a C++17 voxel game prototype using SDL2 and OpenGL 3.3 Core.

## v1.3.0 world seeds and collision

Swept AABB collision blocks all six faces without converting downward motion into an upward displacement. Horizontal movement never changes Y; existing overlaps are handled separately by the nearest valid recovery along six directions.

World generation now uses a uint64 seed, independently derived simplex fields, warped fractal mountains and 3D caves with natural entrances. Player position is chunk-int64 plus local-double; rendering is rebased to the player chunk. Supported gameplay coordinates are within +/-2^40 blocks. Texture variants remain anchored to absolute world coordinates.

```powershell
./build-cmake/Bloxide.exe --world ./worlds/demo --seed 130
./build-cmake/Bloxide.exe --world ./worlds/demo
./build-cmake/Bloxide.exe --help
```

A new world without --seed chooses a random seed once. The default world lives under the user directory supplied by SDL_GetPrefPath, outside build output. world.meta stores seed, generator_version=simplex-1, preset and complete noise configuration. Reopening uses this metadata; conflicting --seed, malformed metadata or unsupported versions are rejected. Only metadata persists: block edits still live in RAM and are lost when the game exits.

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

The mountains-v1 preset combines independently seeded fields:

```text
H = floor(clamp(64 + 72*continental + 58*ridge + 12*billow - 10*turbulence, 24, 192))
```

Continental fBm uses scale 1536/5 octaves, ridges 512/5, billow 384/4 and turbulence 256/3. A 2D warp uses scale 1024/3 octaves and strength 64. Lacunarity is 2 and gain 0.5. Each column starts as Bedrock at Y=0, Stone at Y=1..H-4, three Dirt layers, Grass at Y=H and Air above.

Warped 3D fBm caves are sampled on a world-aligned 4-block grid and trilinearly interpolated. Cave warp is scale 96/3 octaves, strength 12; density is scale 48/4 octaves with threshold 0.38. Nyquist filtering omits density's highest octave at footprint 4. Carving starts at Y=4 and can intersect the surface. Bedrock and Y=1..3 remain protected during generation.

Full occupancy meshes are used throughout render radius 8 so cave entrances are not covered by heightmap LOD. Streaming, greedy meshing and mesh swapping remain active.

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

The CPU suite covers seeded terrain/layers/caves, 2D/3D noise goldens and no-allocation sampling, metadata round trips, DDA targeting, six-face collision, material friction, PNG validation, journal reload, atomic swaps and floating origin. Assertions remain enabled in Release tests. Reproducibility compares sequential/reversed/threaded generation across separate processes.

```powershell
cmake -S . -B build-cmake -DBLOXIDE_RENDER_TESTS=ON
cmake --build build-cmake --parallel 4
ctest --test-dir build-cmake --output-on-failure
./build-cmake/Bloxide.exe --smoke-test
./build-cmake/BloxideBenchmark.exe
```

The optional hidden-window OpenGL test checks every texture layer and mip dimension, then compares repeated shader samples on large quads for six faces, signed coordinates and AO. It also checks terrain/outline pixels, outline occlusion, crosshair, delayed remesh, failed-upload retry and retired buffers. Near/far review PNGs are saved in the executable's diagnostics directory during the test.

The benchmark writes build/benchmark/v1.2-streaming.csv, v1.2-summary.json and v1.2-trace.json. It measures CPU driver work and edit-to-publish latency with simulated upload acknowledgement; it is not a GPU or gameplay-FPS measurement. Use --quick for a short run.

```powershell
./build-cmake/BloxideNoiseBenchmark.exe
./build-cmake/BloxideNoiseBenchmark.exe --probe --seed 130 --reverse --threads
```

The v1.3 benchmark writes build/benchmark/v1.3-noise.csv with simplex/fractal throughput, cave generation/meshing, cold/traversal settling, driver spikes and publication latency. GPU regressions also render near positive and negative world limits. No vegetation, structures, water or disk edit saves are implemented yet; their feature seeds are reserved.

See [ARCHITECTURES.md](ARCHITECTURES.md), [PERFORMANCE.md](PERFORMANCE.md), [CHANGELOG.md](CHANGELOG.md) and [LICENSE](LICENSE).
