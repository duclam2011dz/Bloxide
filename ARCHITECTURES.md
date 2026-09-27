# Bloxide architecture

## Runtime flow

`main` creates `Game`. `Game` owns the SDL window/context, `World`, `Camera` and `Renderer`. Each frame it processes SDL events, updates movement and gravity, renders the current world mesh, and swaps the window buffer.

## Modules

- `World`: fixed-size block storage, bounds-safe access, flat-world initialization and raycast interaction.
- `Block`: block types, solid-block rules and display colors.
- `Camera`: FPS position/orientation and view/projection matrices.
- `Renderer`: visible-face mesh generation, OpenGL buffers, shader program and draw calls.
- `Game`: SDL lifecycle, input, movement, jumping and block placement/removal.
- `third_party/glad`: small vendored OpenGL function loader used after SDL creates the context.

## Rendering data flow

World changes trigger `Renderer::rebuildMesh`. The renderer emits two triangles for every exposed block face, uploads the vertex list to one VBO, and draws it with a VAO. The vertex shader applies camera matrices; the fragment shader outputs the per-face block color.

## v1.0 boundaries

The world is one fixed chunk and is not persisted. There is no texture atlas, chunk streaming, procedural terrain, networking or inventory UI. These can be added behind the existing `World` and `Renderer` interfaces in later versions.
