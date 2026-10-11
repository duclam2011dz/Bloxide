# Bloxide architecture - v1.2.5

## Ownership and frame order

Game owns SDL/OpenGL, World owns chunk metadata/data/journals/groups, Renderer owns GPU buffers, and PlayerPhysics owns the body. World maps and records are owned by the main thread. Workers receive immutable terrain and neighbor snapshots; queue/result locks only guard transfers. Two generation threads and two meshing threads perform CPU work.

A frame processes events, reconciles streaming/results, stages uploads within a soft 2 ms budget, publishes complete groups, updates player/camera, validates queued clicks, computes the next target, renders terrain/outline/crosshair and presents. OpenGL resources are released before context destruction.

## Mesh lifecycle

ChunkRecord keeps activeMesh independently of job state and pendingMesh. A visible chunk remains visible through Generated/Meshing/Uploading and failures. ChunkMesh carries instanceId, revision, groupId, terrain sourceVersion and LOD. Instance IDs distinguish unload/reload; mesh revision advances for terrain, neighbor and LOD changes.

An interior edit invalidates only its chunk. A border edit includes loaded dependent cardinal neighbors. Invalidations merge intersecting groups, invalidate their previous results and capture current snapshots. Generation supplies a one-chunk border apron before meshing. Mesh failure retries up to three times while retaining active geometry.

Renderer::upload builds a separate candidate VAO/VBO/EBO, checks GL errors and stages it without destroying active resources. World::markUploaded validates the candidate token. World::readyGroups exposes only groups whose latest candidates are all acknowledged. Renderer::publish verifies every staged member, swaps all GPU meshes before the frame's draws, and World::commitGroup publishes the matching CPU meshes. Stale results cannot commit.

Old GPU meshes enter a retirement list with GL fences. Zero-timeout polling releases signaled resources without waiting. Unload discards pending resources and retires active ones; shutdown releases all lists while the context is current. No gameplay glFinish is used.

## Targeting and input

traceVoxels uses normalized voxel DDA with deterministic X/Y/Z tie ordering. BlockTarget contains block position, entry normal, previous cell, hit distance, material, instance ID and revision. Reach is measured from the eye to the entry face and includes exactly 6 blocks.

World::target stops at the first solid voxel or at any unavailable/locked chunk. A chunk is targetable only when its active revision matches current data/dependencies and no update group is pending. Bedrock can be outlined but cannot be broken.

Mouse events queue commands containing the previously presented BlockTarget. After movement/camera updates, validation must still match block, face, material, instance and revision; stale clicks do not retarget. Place uses the entry normal and rejects occupied, unloaded, out-of-height or player-overlapping cells.

Outline rendering expands the block slightly and builds screen-space triangle ribbons for its 12 edges. The depth-tested black pass uses no depth writes; hidden edges remain occluded. The centered white crosshair is a separate overlay. This avoids reliance on wide-line driver support.

## Terrain and LOD

surfaceHeight uses positive modulo 128 before evaluating floor(64 + 10*sin(2*pi*x/128) + 6*cos(2*pi*z/128)). Generation derives world positions from integer chunk coordinates, produces continuous periodic hills, and applies RAM journals afterward. Bedrock is Y=0, Grass is Y=H, Dirt occupies H-3..H-1 and Stone fills below.

LOD1 derives a conservative heightmap from the block snapshot: interior patches up to 4x4 take maximum height while the outermost block ring stays exact. Greedy meshing emits both tops and vertical steps from this coarse occupancy. Exact border heights preserve interfaces with LOD0 and neighboring LOD1. Edited chunks force LOD0; crossing the radius-4 threshold invalidates mesh only.

Packed vertices retain X/Z=16 and Y=256 using X bits 0..4, Y 5..13, Z 14..18, base-layer 19..23, face 24..26 and AO 27..28. Triangle winding remains outward. Base-layer replaces the old material field but keeps the vertex at 4 bytes.

## Procedural assets and Texture Array

BloxideTextureGenerator is an explicit C++ CLI. It combines periodic value noise, domain-warped four-octave fBM, toroidal cellular distances, gravel/crack/blade masks and Bayer 4x4 dithering into material-specific 8-12-color palettes. Seeded interior variations blend into a shared border. Grass side has a directional grass-to-dirt mask; all other material tiles wrap both axes.

The generator writes 20 opaque RGBA8 PNGs with LodePNG auto_convert disabled, a manifest, an enlarged contact sheet and mixed-variant 3x3 previews. The manifest schema is BLOXIDE_TEXTURES / version 1 / width / height / layer count / uint32 seed / generator version, followed by layer/group/variant/filename entries. Loader checks uniqueness, mapping and all 20 entries, inspects the original PNG color format, validates 32x32 RGBA8 before decoding and rejects nonopaque pixels. Decoding is bounded. LodePNG sources and license are pinned in third_party/lodepng/UPSTREAM.md.

Runtime assets and shaders are refreshed into the executable directory on every build. SDL_GetBasePath resolves the runtime root. Startup reports bad assets and stops; there is no silent placeholder or runtime generation.

Array base layers are Grass top 0, Grass side 4, Dirt 8, Stone 12 and Bedrock 16. Each group has four consecutive variants. Greedy merge keys use base-layer, face and AO, not variant or original block type. Grass bottom can merge with Dirt when the remaining key matches.

Renderer uploads GL_RGBA8 layers into GL_TEXTURE_2D_ARRAY with GL_REPEAT, GL_NEAREST magnification and GL_NEAREST_MIPMAP_LINEAR minification. It generates six mip levels (32 down to 1). The vertex shader decodes flat uint base-layer/face attributes and computes unwrapped block-space UV. The fragment shader uses an inward face offset to identify the owning voxel, hashes signed world coordinates/base-layer/texture seed to choose a variant and samples with textureGrad using continuous UV derivatives. AO modulates texture color; outline/crosshair remain separate passes.

The texture object is released while the GL context is current. The array payload is 81,920 bytes at level zero and 109,200 bytes including mipmaps, excluding driver overhead.

## Physics and verification

The v1.1.5 player body remains 0.6x1.8x0.6 with eye offset 1.62, gravity 24 blocks/s^2, terminal speed 40 blocks/s and grounded jump speed 7 blocks/s. Axis collision uses substeps at most 5 ms; unloaded columns remain barriers. Spawn scans actual loaded terrain.

PhysicsSettings supplies speed, accelerations, drag and material friction. Horizontal velocity approaches the requested direction with a vector acceleration limit of 45 blocks/s^2 grounded or 12 airborne. Grounded zero-input braking uses exp(-coefficient*dt), choosing the maximum coefficient among actual supporting blocks: Grass 14, Dirt 12, Bedrock 11, Stone 9 s^-1. Air drag is 0.15 s^-1; zero-input horizontal speed below 0.01 is snapped to zero on ground. These operations run in the collision substeps. Input normalization prevents diagonal speed gain, and collisions clear only the blocked velocity axis.

Edit collision changes immediately. The old visual snapshot is temporarily retained during mesh work, with interaction locked until group publication. This deliberate interval is measured as edit-to-publish latency.

CPU tests exercise periodic terrain, exact LOD boundaries, DDA limits/occlusion, target expiration, active-mesh retention, partial upload, overlapping groups, stale tokens, instance reload and LOD transitions. The optional framebuffer test exercises visible/occluded outlines, crosshair, delayed break/place remeshing, failed-upload retry and fence collection. Benchmark CSV/JSON/trace files record CPU driver timings and publication latency, not hardware GPU utilization.
