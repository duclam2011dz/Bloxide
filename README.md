# Bloxide

C++17 voxel prototype, SDL2 and OpenGL 3.3 Core. v1.5.0 focuses on portable development,
resource lifecycle, reproducible diagnostics and distribution, not new gameplay.

## Support and prerequisites

Release targets: Windows 10/11 x64 (MSVC or MSYS2 UCRT64 GCC), Ubuntu 22.04+ x64
(GCC/Clang, X11 or XWayland). macOS ARM64 is experimental; OpenGL is deprecated there.
Platform support is conditional on the actual qualification report, not a successful local build.

Install Git, CMake >=3.25, Ninja and a C++17 compiler. OpenGL comes from your OS/GPU driver,
not vcpkg. Linux additionally needs X11/OpenGL development packages; see DEVELOPMENT.md.
vcpkg/SDL2 are pinned by the repository. Normal builds do not run Python or texture/GL generation.

## Clean clone

Clone this repository, then bootstrap the pinned dependency provider:

```sh
git clone https://github.com/microsoft/vcpkg build-tools/vcpkg
git -C build-tools/vcpkg checkout a077941aaf7e15e2ac228f5052a1d9a65179ff63
# Linux/macOS:
export VCPKG_ROOT="$PWD/build-tools/vcpkg"
"$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics
cmake --preset linux-gcc
cmake --build --preset linux-gcc --parallel 3
ctest --preset linux-gcc
```

Windows, from an MSVC x64 developer PowerShell:

```powershell
$env:VCPKG_ROOT = (Resolve-Path build-tools/vcpkg).Path
& "$env:VCPKG_ROOT/bootstrap-vcpkg.bat" -disableMetrics
cmake --preset windows-msvc
cmake --build --preset windows-msvc --parallel 3
ctest --preset windows-msvc
./out/windows-msvc/Bloxide.exe --version --json
```

For UCRT64 use windows-gcc from a shell with its compiler/Ninja on PATH.
For Linux Clang use linux-clang; macOS ARM64 uses macos-arm64. Presets debug,
release-o2, release-o3, scalar-only, asan-ubsan and tsan are also available.
Platform presets can override configuration, e.g. -DCMAKE_BUILD_TYPE=Debug.
ASan+UBSan and TSan are Linux Clang builds and must not be combined.

## Play and diagnostics

```sh
./out/linux-gcc/Bloxide --world ./worlds/demo --seed 130 --preset mountainous
./out/linux-gcc/Bloxide --validate-assets
./out/linux-gcc/BloxideProfiler --baseline --output ./out/baseline
./out/linux-gcc/BloxideProfiler --suite all --quick --lod off --output ./out/smoke
```

Windows executables have the .exe suffix. --help/--version do not initialize SDL/GL/world workers;
--validate-assets validates the shipped 112 PNG layers without a display.
Assets/shaders resolve beside the executable. Default worlds remain in the user data directory;
diagnostics/benchmarks use writable user-data subdirectories, never the install prefix.
Use explicit --output/--benchmark-output/--diagnostics-dir for automation.

WASD/mouse, Space jump, mouse buttons break/place, Esc mouse capture. Space/Ctrl swim.
Hotbar 1-9/scroll; F3 DevTools, F3+Tab interactive tools, Esc restore capture.
Water is not directly placeable. Edits and fluid changes are **RAM-only and lost on exit**.

Existing worldgen versions/metadata are not migrated. Determinism is tested within toolchain
profiles; identical seed/coordinates are not promised to produce identical terrain across
different compilers/libm/architectures. Reference Windows GCC output is preserved.

## Tests, install and packages

```sh
cmake --preset linux-clang -DBLOXIDE_RENDER_TESTS=ON
cmake --build --preset linux-clang --parallel 3
ctest --test-dir out/linux-clang -L headless --output-on-failure
xvfb-run -a ctest --test-dir out/linux-clang -L gpu --output-on-failure --parallel 1
cmake --install out/linux-clang --prefix ./out/install
cpack --config out/linux-clang/CPackConfig.cmake -B out/linux-clang/packages
```

Linux ZIP-equivalent distribution is TGZ with relative RPATH; Windows uses ZIP. Packages include
runtime dependencies where permitted, assets/shaders, notices, docs, build manifest and SHA-256.
They do not include GPU drivers/glibc. Linux packages require Ubuntu 22.04+ compatible glibc,
X11/XWayland and a GL 3.3 driver. Full Mesa CI is correctness coverage, not hardware FPS evidence.

See DEVELOPMENT.md, CONTRIBUTING.md, CODE_STYLE.md, ARCHITECTURES.md, PERFORMANCE.md and docs/adr.
Historical v1.4 setup/implementation notes are archived in docs/history/v1.4-README.md;
machine paths in historical documents are not current setup instructions.
