# Bloxide architecture - v1.4.0

## Current generation and ownership

`WorldSettings` selects immutable `ChunkGenerator` / `DensityWorld` contexts before workers start. Metadata schema 1/simplex-1 and schema 2/terrain-fields-1 remain unchanged; new schema 3/density-world-1 records density, climate, aquifer and feature parameters. Seed domains are appended, never renumbered.

Density combines the shared height envelope with band-limited warped 3D perturbations and independently masked caves. World-aligned four-block node grids interpolate occupancy; protected Y=0..3 and ceiling Y=248 are preserved. Halo occupancy supplies exposed surface/depth/slope rules. Optimized generation caches field/octave parameters, proves uniform Air/Solid sections before skipping interpolation, bounds column fills and stamps precomputed ore candidates. Reference/scalar results must match exactly.

Ore ellipsoids belong to signed world-aligned 16-block cells. Trees use stable cell priorities/exclusion; village region descriptors reserve bounds and reject unsafe support/foundation sites. Each chunk stamps only intersections with deterministically sorted descriptors. Immutable feature IDs and precedence make generation order irrelevant. Features are not simulated entities.

`BlockState` is uint16: stable block ID in the low byte and auxiliary state in the high byte. Water encodes level/falling flags. Summary histograms count full water states and materials; tree/village counts are chunk incidences, not unique-world totals.

### Scheduling and budgets

Main thread owns world maps, journals, tile records and publication transactions. Two generation workers handle near jobs (batches <=2) and far subjobs (<=4); two meshing workers handle greedy meshes/hierarchies. Workers use immutable block/journal snapshots and per-thread scratch, with cancellation between phases. Instance/revision/group tokens reject stale jobs.

Shared credits bound queued generation/meshing at 128/64. Upload staging has 64 leases acquired before meshing and released after accepted GPU upload, stale rejection or cancellation. OpenGL never runs on workers. CPU accounting includes active/pending/previous meshes and completed worker results; fixed near-worker reservations plus per-far-job reservations prevent admission from treating temporary work as free. Authoritative RAM journals are measured separately and never evicted.

Cache/GPU payload defaults are 512/128 MiB. Pressure reduces effective far radius, canceling regenerable low-priority work; recovery requires two seconds below headroom thresholds plus room for the next tile ring and a far-job reservation. Near data remains required even when a user-selected budget is too small. Tracked payload excludes allocator/driver overhead, so process working set/private commit are displayed separately.

### Adaptive surfaces and publication

Production far jobs use near snapshots where available, otherwise temporary generation arrays, apply RAM journals and release arrays after meshing. No persistent full ChunkData is retained for far tiles. Boundary occupancy shells stay full resolution, including vertical detail.

Fine greedy surfaces become indexed 40-byte SurfaceVertices (XYZ target/source, integer material/face/AO/anchor). QEM ranks candidate collapses, but manifold links, material/cell boundaries, winding and intersection tests decide validity. Triangle orientation is checked analytically over the entire linear morph. Non-adjacent swept convex hulls must admit a separating axis; uncertain cases fall back to fine geometry. Material/translucent details are conservatively retained instead of flattened.

XYZ parent mappings interpolate for 250 ms. Interrupted transitions reuse currently displayed positions only when source signatures agree. Fixed shared boundaries, near/far ownership transactions and revision-checked groups retain active meshes until replacements can publish together. CPU meshes retain mapping data; after morph completion GPU buffers compact to the accepted coarse surface. Retired buffers use nonblocking fences.

### Rendering and input

Packed opaque/cutout vertices remain uint32: X 0..4, Y 5..13, Z 14..18, base layer 19..25, face 26..28, AO 29..30. Separate float vertices handle plants/fractional water/adaptive geometry. The Texture Array has 112 RGBA8 layers with immutable first-20 mapping, per-material alpha policies and coverage-preserving cutout mipmaps. World-block UV repetition and int64 high/low hashing survive floating-origin rebasing.

Opaque writes depth; cutout discards alpha below 0.5; globally sorted translucent faces blend without depth writes. Underwater fog and water UV animation are independent of world seed. Flowers use selection boxes/cross planes without collision; water is skipped by DDA.

Solid physics keeps swept AABB/material friction/air control. Swimming queries actual submerged volume and uses separate tuning. FluidEngine ticks at 10 Hz with a bounded active frontier, two-phase sorted updates, down-first flow and source conversion. Unloaded chunks block flow; out-of-radius frontiers freeze. Journals preserve state only until process exit.

### Diagnostics

Dear ImGui v1.92.9b is pinned in third_party/imgui/PINNED.md with MIT license and official SDL2/OpenGL3 backends. F3+Tab transfers input ownership, clears gameplay commands and releases relative mouse mode; Esc restores prior capture. Simulation continues. Heatmap sampling is budgeted; village lookup/capture writing run off the main thread.

DevTools history caps at 4096 frames; trace ring at 65536 events; async writer queue at two snapshots of at most 16 MiB each. Overwritten/dropped events and incomplete captures are explicit. Runtime percentile samples cap at one million; screenshots have a 32 MiB pixel cap and can stall during readback/encoding.

GPU timestamp queries use an eight-slot asynchronous ring with source frame IDs and opaque/cutout/translucent phases. They measure elapsed GPU command intervals, not shader-stage utilization. Windows telemetry samples process/system CPU and RAM at 1 Hz; adapter-verified DXGI/PDH metrics are capability-dependent. Unsupported data is N/A.

Frame duration is the start-to-start interval ending at the current frame start, so it includes prior-frame capture/logging/driver overhead. CPU phase columns describe the current submission frame; GPU columns carry delayed source IDs. The complete GPU-frame command interval can include scheduling/idle gaps and is not equivalent to pure GPU busy time.

## Historical v1.3.5 implementation notes

The following sections describe legacy generator/render formats retained for compatibility and regression fixtures, not the v1.4 production far pipeline.

## Coordinate and collision contracts

WorldPosition is chunk X/Z int64 plus local X/Z double in [0,16), with Y double. Block positions and raycast targets use int64 X/Z and integer Y. Gameplay spans +/-2^40 blocks; block cells are [-2^40,2^40). Checked normalization and conversions prevent signed overflow and nonfinite input.

PlayerPhysics keeps a local-double AABB and an origin chunk. Queries translate local voxel indices to absolute int64 block coordinates. Swept collision examines forward faces intersecting the swept interval, requiring strict overlap in the other axes. Allowed displacement stays between zero and requested delta. X/Z never modify Y; only downward contact sets grounded. Overlap recovery is separate, tries six directions and uses the nearest clear displacement with fixed ties. The old solver could choose a positive Y displacement while falling after tiny side penetration; a 2e-5 overlap fixture locks down that regression.

Camera matrices and chunk/outline coordinates are relative to the player origin. Rebase changes the representation, not physical position, velocity or target. The far plane is 768 blocks for distant radius 24. Shader texture hashing receives low/high uint32 words for absolute chunk-block X/Z and performs signed local additions without OpenGL int64 support.

## Seed and simplex context

WorldSettings validates seed/configuration before a World constructs its workers. The ChunkGenerator holds immutable settings and ten noise fields. SplitMix64 derives domains for continental, ridges, hills, turbulence, warp X/Z, cave density, cave warp X/Y/Z and reserved structures/vegetation.

Each simplex field seeds Fisher-Yates with unbiased bounded SplitMix64 output once. Sampling hashes full signed int64 lattice coordinates with unsigned arithmetic and uses the permutation table for gradient selection. It does not rely on coordinate modulo 256 for field repetition.

2D uses skew/unskew and three corners; 3D selects a tetrahedron and four corners. Fixed normalized gradient tables and max(0,t)^4 contributions are scaled by 99 (2D) and 45 (3D), clamped to [-1,1]. Finite sample inputs are limited to +/-2^44 before lattice conversion. Valid sample calls allocate no memory.

FractalSettings provides octaves, frequency, lacunarity and gain. FractalNoise supports normalized fBm, billow, turbulence, ridged feedback and 2D/3D warps. Frequencies above 0.5/footprint are omitted, and retained amplitude weights are renormalized. Core floating-point contraction/fast-math is disabled to support exact block reproducibility across Debug/Release on the supported toolchain.

## Terrain and metadata

The simplex-1 generator uses mountains-v1: surface warp 1024/3 octaves, strength 64; continental 1536/5, ridge 512/5, billow 384/4 and turbulence 256/3. Height is floor(clamp(64+72*continental+58*ridge+12*billow-10*turbulence,24,192)). Default lacunarity/gain are 2/0.5.

Generation builds 256 column heights once, fills Bedrock/Stone/three Dirt layers/Grass and leaves Air above. Caves use a 4x4x4 world-aligned density cache with required boundary nodes and trilinear interpolation. Warp is 96/3 octaves and strength 12; density is 48/4 with threshold 0.38, filtered at footprint 4. Y=0..3 is protected, and caves may carve the surface. Cache allocation happens per job, not per noise sample.

Full occupancy meshes replace heightmap LOD throughout radius 8. No distance-dependent terrain regeneration or cave-mouth masking occurs. Structures/vegetation domains are reserved only.

CLI --world/--seed opens world.meta or creates it once. Normal default storage comes from SDL_GetPrefPath; smoke tests use distinct executable-local fixture directories. Metadata text schema 1 stores uint64 seed, generator version, preset, all seven fractal configs and warp/threshold values. Classic locale and max_digits10 preserve numeric round trips. Save uses a same-directory temporary file and atomic replacement; unknown versions, invalid config, extra/duplicate keys and seed conflicts are errors. No edit journal is saved to disk.

## Streaming, rendering and textures

New worlds use schema 2/terrain-fields-1, storing roughness, mountain mask, valley and plateau configurations, monotone spline points, amplitudes, mask thresholds, clamp bounds and caves flag. Schema 1/simplex-1 remains supported without rewriting it. New seed domains append to the enum, preserving old seeds. --preset and --caves apply at creation; metadata conflicts are rejected.

sampleTerrain(worldX,worldZ,footprint) is the shared continuous height API. Voxel heights floor footprint-1 samples. Terrain-fields-1 blends a monotone continental spline, smooth mountain mask, roughness-controlled hills/ridges, valley shaping and plateau weighting. Flat returns 64 without height noise. Cave carving remains independent of distant sampling.

Each voxel job builds an 18x18 height halo and section occupancy counts. Direct writes bypass gameplay edits. Uniform Stone sections fill contiguously; Air sections are untouched; cave grid extrema conservatively skip uniform cells/sections. Reference generation disables those skips for byte comparison. GenerationState is separate from ChunkState: Requested, Generating, Ready, Cancelled, Failed. Jobs capture immutable configuration and instance/generation revision; cancellation is checked inside generation.

### Heightmap streaming and publication

HeightmapStream owns distant metadata on the main thread. Its jobs share the two generation workers with voxel jobs; two separate workers mesh voxels. Queue locks cover extraction/publication only. Distance/camera priority and aging prefer nearby work; every eighth mixed-queue turn permits distant work. Cancellation tokens are job-specific shared atomics, so superseding a tile cannot redirect an old worker to a new token.

Completed results are pumped every frame, but request/border scans run only when the player chunk or voxel border revision changes (or a failed job needs retry). Publication explicitly retires the opposite CPU representation at that coordinate, preventing immediate reversals from selecting a CPU mesh whose GPU buffers have already been replaced.

Game enables far streaming; World's optional heightmap constructor flag defaults off for existing near-only headless callers. Far radius is 24, with 16x16 tiles and steps 2/4/8; no ChunkData is created for far tiles. Hysteresis holds step 2 through radius 13 and step 8 down to radius 18.

All far edges sample footprint 1 and have one-block segments, giving adjacent LODs identical topology. Interiors use their LOD footprint, with transition triangles to fixed edges. Canonical corners remain shared; voxel boundary snapshots supply actual column heights and vertical wedge closure. Eight-block skirts are fallback at outer/voxel boundaries, not a replacement for stitching.

Heightmap meshes use separate 24-byte vertices: local float XYZ, source morph Y, integer layer and face. The shader interpolates interior height over 250 ms; edges remain fixed. New vertices sample the currently displayed previous mesh, including an interrupted morph. Dependent tiles share publication time. Near/far handoffs and boundary dependencies combine upload groups into one transaction; old meshes remain until replacements publish. Mesh-only near records can survive outside voxel retention during handoff without retaining block arrays.

Upload candidates are collected/sorted before starting the soft 2 ms OpenGL budget; otherwise a large Debug queue can exhaust it before the first upload and starve publication. Buffers retire with nonblocking fences. Renderer uses conservative homogeneous AABB frustum culling and polls an eight-slot terrain timer-query ring without waiting. Heightmaps are visual approximations only: no distant collision, targeting, caves or edits.

World maps, journals, records and update groups are owned by the main thread. Two generation and two meshing workers consume immutable snapshots through short queue/result locks. Chunk instance/revision/group tokens reject obsolete results.

Active meshes remain independent of pending job state. Border edits merge affected groups; separate GPU buffers upload within a soft 2 ms budget, then complete groups publish before drawing. Old buffers retire through zero-timeout fence polling. Collision sees edits immediately while targeting is locked until publication.

Voxel DDA uses local-double traversal with absolute int64 targets and reach 6. Queued clicks validate the previously presented block, face, material and revision. Placement rejects overlap with the player. Black outline uses depth-tested triangle ribbons, and the crosshair is a separate overlay.

The 20-layer RGBA8 Texture Array and strict 32x32 PNG loader remain. Packed vertices use X bits 0..4, Y 5..13, Z 14..18, base-layer 19..23, face 24..26 and AO 27..28. Greedy keys exclude per-block variants; UV repeats at block scale. Texture seed 125 remains independent of world seed, and absolute-coordinate hashing survives rebasing.

## Verification

Noise goldens, generator byte digests, numerical guards and an allocation-count test lock down simplex-1. Separate processes compare chunk order and threading for seeds 0, 130 and uint64 max; Debug/Release hashes are also compared exactly on the supported compiler. Cave interpolation is tested across positive/negative chunk boundaries.

Physics regressions cover six faces, epsilon contacts, side-overlap recovery, chunk seams, negative coordinates and world limits. Metadata tests cover round trips, random-once reopening, conflicts, unsupported versions and failed atomic writes. OpenGL tests validate layers/UV/AO, rebased texture hashing and terrain/interaction near both world limits.

BloxideNoiseBenchmark records sample throughput, generation/meshing, cold/traversal completion, memory payload, driver spikes and edit publication. Headless upload acknowledgement is simulated; these timings are not gameplay FPS or GPU utilization.

V135 tests compare reference/optimized blocks for all presets/cave modes, halo seams, metadata compatibility, mixed-LOD edges, morph endpoints, winding, cancellation and streaming without far voxel allocation. TerrainBenchmark exports CSV/JSON for LOD on/off and traversal/reversal. Runtime diagnostics separate main-thread phase times, cumulative worker elapsed work and asynchronous GPU terrain time. OpenGL fixtures exercise integer heightmap attributes, morph geometry and six settled moves/reversals with live GPU ownership validation.
