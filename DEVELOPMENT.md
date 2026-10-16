# Development

The v1.5 portability work targets CMake >=3.25, Ninja and C++17.
The canonical dependency provider is vcpkg at the revision in vcpkg.json; VCPKG_ROOT is supplied by
the developer, never a hardcoded checkout location. See README for commands and support status.

Windows: use either an MSVC developer shell or MSYS2 UCRT64 GCC/Ninja, not a mixture of CRTs.
Linux: Ubuntu 22.04+ baseline, GCC/Clang, X11 development libraries and OpenGL/Mesa development files.
Wayland sessions require XWayland. macOS ARM64 remains experimental.

Assets/shaders are executable-relative. Worlds and diagnostics belong in writable user directories,
not the install prefix. Tests and benchmarks must explicitly isolate their worlds and outputs.
Use Release builds for performance, separate ASan+UBSan and TSan builds for correctness.

Support is earned by actual CI/package qualification, not inferred from successful local compilation.
Historical v1.4 measurements are in PERFORMANCE.md and must not be relabeled v1.5.
