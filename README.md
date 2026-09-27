# Bloxide

Bloxide is a small Minecraft-inspired voxel prototype written in C++17 with SDL2 and OpenGL 3.3 Core.

## v1.0 features

- SDL2 window and OpenGL 3.3 renderer
- Flat 32×16×32 voxel world
- Grass, dirt and stone blocks
- FPS camera with WASD, mouse look and jump
- Left-click removes a block; right-click places grass
- Headless world smoke test through CTest

## Requirements

Windows with [MSYS2 UCRT64](https://www.msys2.org/) is the supported development environment. The current setup expects GCC, CMake and SDL2 to be available under `D:/msys64/ucrt64`.

## Build

Run these commands from an MSYS2 UCRT64 shell:

```bash
cmake -S . -B build -G "MSYS Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/ucrt64
cmake --build build
ctest --test-dir build --output-on-failure
./build/Bloxide.exe
```

From PowerShell, use the installed CMake and pass the Windows prefix explicitly:

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH=D:/msys64/ucrt64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

If the MSYS2 CMake binary crashes before printing its version, install the official Windows CMake package and use that executable for the commands above. The compiler can remain `D:/msys64/ucrt64/bin/g++.exe`.

## Controls

WASD moves, the mouse looks around, Space jumps, left mouse removes a block, right mouse places grass, and Esc releases or captures the mouse.

## Troubleshooting

- If CMake cannot find SDL2, verify `D:/msys64/ucrt64/lib/cmake/SDL2/SDL2Config.cmake` exists and pass `-DCMAKE_PREFIX_PATH=D:/msys64/ucrt64`.
- If the executable cannot find SDL2 at runtime, add `D:/msys64/ucrt64/bin` to `PATH` or copy `SDL2.dll` beside the executable.
- An OpenGL 3.3-capable graphics driver is required.

## Project documents

- [ARCHITECTURES.md](ARCHITECTURES.md) describes the runtime modules and data flow.
- [CHANGELOG.md](CHANGELOG.md) records releases.
- [LICENSE](LICENSE) contains the MIT license.
