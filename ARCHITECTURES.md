# Bloxide architecture — v1.1.5

## Runtime and ownership

Game owns SDL/OpenGL, World owns chunk data and CPU job queues, Renderer owns GPU resources, and PlayerPhysics owns the player body. Each frame integrates streaming results, uploads pending meshes, updates physics, updates the camera and renders ready chunks. Two generation workers and two meshing workers run CPU work; only the main thread calls OpenGL.

World hashes ChunkCoord with signed int64 X/Z. Chunk states follow Unloaded -> Queued -> Loading -> Generated -> Meshing -> Uploading -> Ready, with unloading/cancellation branches. Job version checks reject obsolete results. Edits publish copied ChunkData snapshots so workers can finish safely on the old snapshot. Neighbor changes invalidate mesh jobs and queue remeshing.

## World and edits

Chunks are 16x256x16 with block Y=0..255. Terrain is Bedrock at Y=0, Stone at Y=1..61, Dirt at Y=62, Grass at Y=63 and Air above. Generation/render radii are 10/8 using Chebyshev distance; simulation radius 6 is currently a configuration constant, with no independent block tick system.

Block edits are journaled in RAM. On reload, packed journal keys are decoded into X/Y/Z and applied through ChunkData::set. This preserves block indexing and Bedrock protection. Block edits and neighbor updates remesh without regenerating terrain.

## Geometry and OpenGL

Greedy meshing removes hidden faces against center/neighbor snapshots. The 32-bit vertex stores X in bits 0..4, Y in 5..13, Z in 14..18, material in 19..21, face in 22..24 and AO in 25..26. Vertex coordinates include 16 and 256 even though block indices stop at 15 and 255. Shader decoding matches this layout. Indices are uint32; all triangles face outward under default CCW front-face semantics.

LOD1 covers the complete unedited flat surface at Y=64. Edited chunks use full geometry. Distance/forward filtering, depth testing and back-face culling remain enabled. Neighbor mesh uploads are accepted even when the terrain version is unchanged.

Renderer owns VAO/VBO/EBO resources and releases them before Game deletes the current OpenGL context. A zero-sized drawable is skipped safely.

## Player physics

PlayerPhysics stores feet position and velocity separately from Camera. Its AABB extends 0.3 blocks on each X/Z side and 1.8 blocks upward; the camera follows feet plus a 1.62-block eye offset. Gravity is 24 blocks/s², fall speed is capped at 40 blocks/s, walk speed is 5 blocks/s and grounded jumping starts at 7 blocks/s.

Updates use substeps no longer than 5 ms, resolving X, Z, then Y against solid voxels. Axis collision clamps displacement and clears that velocity component. Downward collision establishes grounded state; walls and ceilings do not. Spawn waits for chunk data and scans for a real support block with two air cells. Unloaded columns act as collision barriers. Placement rejects blocks overlapping the player body.

## Verification

CTest covers generation, meshing, negative coordinates, packed boundaries, outward normals, gravity, wall sliding, ceiling collision, jumping, support removal, frame-rate variation, high-speed landing and journal reload. Release tests keep assertions enabled.

With BLOXIDE_RENDER_TESTS enabled, a hidden SDL/OpenGL window reads its framebuffer and requires visible terrain pixels. Bloxide --smoke-test prints terrain pixel count, draw count, feet height and grounded state and exits nonzero if rendering fails.

Profiler and BloxideBenchmark export headless CSV/JSON/trace data. These measure CPU streaming work; the game currently does not export runtime frame timings or GPU utilization.
