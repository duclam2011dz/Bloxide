# Bloxide architecture - v1.2.0

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

Packed vertices retain X/Z=16 and Y=256 using X bits 0..4, Y 5..13, Z 14..18, material 19..21, face 22..24 and AO 25..26. Triangle winding remains outward.

## Physics and verification

The v1.1.5 player body remains 0.6x1.8x0.6 with eye offset 1.62, gravity 24 blocks/s^2, terminal speed 40 blocks/s and grounded jump speed 7 blocks/s. Axis collision uses substeps at most 5 ms; unloaded columns remain barriers. Spawn scans actual loaded terrain.

Edit collision changes immediately. The old visual snapshot is temporarily retained during mesh work, with interaction locked until group publication. This deliberate interval is measured as edit-to-publish latency.

CPU tests exercise periodic terrain, exact LOD boundaries, DDA limits/occlusion, target expiration, active-mesh retention, partial upload, overlapping groups, stale tokens, instance reload and LOD transitions. The optional framebuffer test exercises visible/occluded outlines, crosshair, delayed break/place remeshing, failed-upload retry and fence collection. Benchmark CSV/JSON/trace files record CPU driver timings and publication latency, not hardware GPU utilization.
