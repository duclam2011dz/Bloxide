# Bloxide architecture

## Runtime flow

`Game` owns SDL/OpenGL, `World` owns the chunk map and worker queues, and `Renderer` owns GPU resources. Each frame updates the streaming center, drains CPU job results, uploads a bounded amount of mesh data on the render thread, culls chunks and draws ready meshes.

## Chunk ownership and states

A `ChunkRecord` is metadata plus shared immutable CPU snapshots and a version. State transitions are `Unloaded -> Queued -> Loading -> Generated -> Meshing -> Uploading -> Ready`; cancellation/unload and `Failed` are terminal branches for a stale job. Generation and meshing workers never call OpenGL. Version checks prevent late results from replacing newer edits.

The map is hashed by `ChunkCoord { int64_t x, z }`. Generation and meshing queues are priority queues ordered by chunk distance, camera alignment and starvation-safe age. Map/queue locks are held only while looking up or moving work; generation and meshing operate on owned snapshots.

## World and edits

Chunks are `16x256x16`, with local Y in `0..255`. The deterministic v1.1 terrain has Bedrock at Y=0, Stone through Y=61, Dirt at Y=62 and Grass at Y=63. `EditJournal` records block edits in memory and is applied after generation. Bedrock is rejected by both data and gameplay mutation paths.

## Meshing and rendering

Meshing first removes faces against solid neighbors, including loaded cross-chunk border snapshots. Coplanar compatible faces are merged by greedy meshing. Vertices pack local X/Z, Y, block, face and AO into one uint32; indices are uint32. LOD0 is used near the player and LOD1 uses a coarse surface mesh in the outer render ring. CPU distance/forward culling precedes renderer frustum-style visibility checks; OpenGL depth and back-face culling reduce raster work.

Every chunk has a CPU mesh version and dirty state. A block edit marks mesh dirty without regenerating terrain. The render thread owns VAO/VBO/EBO creation/destruction and consumes `ChunkUpload` results from workers.

## Profiling

`Profiler` records frame and named phase timings and exports CSV, JSON and Chrome trace JSON. `BloxideBenchmark` runs cold-stream, steady-state, traversal and edit-position scenarios. CPU phase percentages are derived from exclusive timings. OpenGL timer queries can be added around terrain passes; hardware GPU utilization is intentionally not reported because it is not portable across OpenGL drivers.
