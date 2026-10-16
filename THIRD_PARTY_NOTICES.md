# Dependency and asset provenance

| Component | Provenance | License |
|---|---|---|
| SDL2 2.32.10 port 1 | vcpkg baseline a077941aaf7e15e2ac228f5052a1d9a65179ff63 | Zlib |
| Dear ImGui v1.92.9b | f1cc2ae15e53a861a874c3034aae6798fde194ab | MIT; upstream embedded font/stb notices retained |
| LodePNG | e584a8490db1b76b81bf298c11c09d3b55d9c270 | zlib |
| GLAD 2.0.8 generated GL 3.3 core | 73db193f853e2ee079bf3ca8a64aa2eaf6459043 | (WTFPL OR CC0-1.0) AND Apache-2.0; Khronos platform header MIT |
| 112 material PNGs and previews | first-party TextureGenerator v1.4.0, seed 125 | project MIT |

Source licenses are retained in third_party and installed under share/licenses/Bloxide.
The ImGui license bundle also carries its font/stb notice-bearing upstream source files;
they are attribution material, not an installed build/source dependency.
GLAD's Apache-2.0/CC0 texts are from SPDX license-list-data v3.25.0, with the unmodified
Khronos platform-header notice. The project chooses CC0 for the generated-code OR license.
GLAD uses its bundled registry with --reproducible and no extensions; normal builds do not generate it.
No external Minecraft textures are included. Asset provenance is the project's generator and git history.

Windows MinGW packages may contain GCC/libstdc++ runtime libraries under GPL with the GCC Runtime
Library Exception, and winpthreads under its upstream notices. Their redistributable license files
must accompany the corresponding package. MSVC packages use static CRT/SDL2 and do not bundle Windows
system DLLs. Linux excludes glibc, display/system GL libraries and GPU drivers.

Package validation must reject missing notices rather than claim dependencies are all project-MIT.
