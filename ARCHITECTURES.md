# Bloxide architecture - v1.3.0

## Coordinate and collision contracts

WorldPosition is chunk X/Z int64 plus local X/Z double in [0,16), with Y double. Block positions and raycast targets use int64 X/Z and integer Y. Gameplay spans +/-2^40 blocks; block cells are [-2^40,2^40). Checked normalization and conversions prevent signed overflow and nonfinite input.

PlayerPhysics keeps a local-double AABB and an origin chunk. Queries translate local voxel indices to absolute int64 block coordinates. Swept collision examines forward faces intersecting the swept interval, requiring strict overlap in the other axes. Allowed displacement stays between zero and requested delta. X/Z never modify Y; only downward contact sets grounded. Overlap recovery is separate, tries six directions and uses the nearest clear displacement with fixed ties. The old solver could choose a positive Y displacement while falling after tiny side penetration; a 2e-5 overlap fixture locks down that regression.

Camera matrices and chunk/outline coordinates are relative to the player origin. Rebase changes the representation, not physical position, velocity or target. The far plane is 384 blocks to accommodate tall terrain across render radius 8. Shader texture hashing receives low/high uint32 words for absolute chunk-block X/Z and performs signed local additions without requiring OpenGL int64 support.

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

World maps, journals, records and update groups are owned by the main thread. Two generation and two meshing workers consume immutable snapshots through short queue/result locks. Chunk instance/revision/group tokens reject obsolete results.

Active meshes remain independent of pending job state. Border edits merge affected groups; separate GPU buffers upload within a soft 2 ms budget, then complete groups publish before drawing. Old buffers retire through zero-timeout fence polling. Collision sees edits immediately while targeting is locked until publication.

Voxel DDA uses local-double traversal with absolute int64 targets and reach 6. Queued clicks validate the previously presented block, face, material and revision. Placement rejects overlap with the player. Black outline uses depth-tested triangle ribbons, and the crosshair is a separate overlay.

The 20-layer RGBA8 Texture Array and strict 32x32 PNG loader remain. Packed vertices use X bits 0..4, Y 5..13, Z 14..18, base-layer 19..23, face 24..26 and AO 27..28. Greedy keys exclude per-block variants; UV repeats at block scale. Texture seed 125 remains independent of world seed, and absolute-coordinate hashing survives rebasing.

## Verification

Noise goldens, generator byte digests, numerical guards and an allocation-count test lock down simplex-1. Separate processes compare chunk order and threading for seeds 0, 130 and uint64 max; Debug/Release hashes are also compared exactly on the supported compiler. Cave interpolation is tested across positive/negative chunk boundaries.

Physics regressions cover six faces, epsilon contacts, side-overlap recovery, chunk seams, negative coordinates and world limits. Metadata tests cover round trips, random-once reopening, conflicts, unsupported versions and failed atomic writes. OpenGL tests validate layers/UV/AO, rebased texture hashing and terrain/interaction near both world limits.

BloxideNoiseBenchmark records sample throughput, generation/meshing, cold/traversal completion, memory payload, driver spikes and edit publication. Headless upload acknowledgement is simulated; these timings are not gameplay FPS or GPU utilization.
