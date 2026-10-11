# Bloxide procedural texture assets

All 20 material PNGs are 32x32, source RGBA8 and fully opaque. Generated with BloxideTextureGenerator v1.2.5, texture seed 125. Texture seeds do not seed terrain.

| Group | Layers | Contact-sheet row |
|---|---|---|
| Grass top | 0-3 | 1 |
| Grass side | 4-7 | 2 |
| Dirt / Grass bottom | 8-11 | 3 |
| Stone | 12-15 | 4 |
| Bedrock | 16-19 | 5 |

Contact-sheet columns are variants 0, 1, 2, 3. tiled_preview.png shows mixed variants in 3x3 repeats, with groups left to right in the table's order.

Noise is periodic; variants share their outer border. Grass side wraps horizontally but has an intentional top grass strip and dirt below. Palettes contain 8-12 colors and Bayer dithering preserves sharp pixel-art transitions.

Regenerate from the repository root:

```powershell
./build-cmake/BloxideTextureGenerator.exe --output assets/textures --seed 125
cmake --build build-cmake --parallel 4
```

The manifest defines fixed layer order independently of directory enumeration. Runtime strictly rejects missing/corrupt PNGs, dimensions other than 32x32, source formats other than RGBA8 and alpha other than 255.
