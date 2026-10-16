# Development and clean-clone verification

## Toolchains

- Common: Git, CMake >=3.25, Ninja, C++17, network access for the first dependency build.
- Windows MSVC: VS 2022 C++ build tools + Windows SDK, x64 developer shell; static CRT/SDL2.
- Windows GCC: MSYS2 UCRT64 GCC/Ninja; dynamic SDL2 plus compiler runtimes. Do not mix MINGW64,
  UCRT64, MSVC runtime libraries or compiler binaries from a different shell.
- Ubuntu baseline: GCC 12 or Clang 15; X11/XWayland. Use the OS's GL implementation/driver.
- macOS ARM64: Xcode command-line tools, Ninja, CMake; experimental scalar-only profile.

Ubuntu prerequisites:

```sh
sudo apt-get update
sudo apt-get install -y git curl zip unzip tar pkg-config ninja-build g++-12 clang-15 python3-venv \
  libgl1-mesa-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxinerama-dev \
  libxfixes-dev libxrender-dev libxss-dev libxxf86vm-dev xvfb mesa-utils patchelf
```

Ubuntu 22.04's original CMake is too old for these presets. Install a current official CMake
or use isolated host tools (not a global Python replacement):

```sh
python3 -m venv out/host-tools
out/host-tools/bin/pip install cmake==3.31.6 ninja==1.11.1.3
export PATH="$PWD/out/host-tools/bin:$PATH"
```

Bootstrap the pinned vcpkg checkout exactly as in README. Set VCPKG_ROOT in the current shell.
Linux examples use cmake --preset linux-gcc -DCMAKE_C_COMPILER=gcc-12 -DCMAKE_CXX_COMPILER=g++-12;
use linux-clang with clang-15/clang++-15 for the CI-matched Clang profile.
Clean-cache builds must work; caches are an optimization, not a required SDK.

## Presets and outputs

Each preset has its own out/<preset> directory. Do not reuse caches between architectures,
dependency providers, CRTs or sanitizer modes. CMakeUserPresets.json is ignored for local paths.
Generic debug/release presets use the active compiler/default vcpkg triplet; platform presets
select the supported triplets. MSVC uses /O2 for both named Release optimization variants,
and metadata records the actual flag rather than claiming a nonexistent /O3.

BLOXIDE_ENABLE_SIMD=OFF builds scalar without x86 intrinsics. An unavailable explicit backend
is rejected. Auto remains scalar. No project-wide native ISA/fast-math/FMA contraction is enabled.

An advanced system-provider build can configure without the vcpkg toolchain:
cmake -S . -B out/system -G Ninja -DCMAKE_BUILD_TYPE=Release.
It needs an SDL2 CMake config and runtime/license discovery; release qualification uses vcpkg.

## Sanitizers and GL tests

Use asan-ubsan and tsan presets separately on Linux with Clang. Run ctest --preset <name>.
Sanitizers diagnose correctness, not performance. TSan on kernels with wider randomized mappings
may require vm.mmap_rnd_bits=28 in the disposable test runner; CI applies this OS compatibility
setting, not a race suppression. Never disable reports to obtain a green build.

Set BLOXIDE_RENDER_TESTS=ON for the five driver-backed regressions. GPU tests are labeled gpu,
headless tests headless. Run GPU tests sequentially. Mesa/Xvfb CI allows up to 30 minutes/test;
hardware GPU results must identify the actual driver and commit.

## Baselines, provenance and assets

benchmarks/baselines/v1.5-fixed.json defines seed/config/camera/resolution/budgets/windows.
BloxideProfiler --baseline runs four near/LOD x overlay variants with five repeats by default.
--quick is only tooling smoke, never a qualified performance window. Compare only compatible
workload/determinism/hardware profiles; inspect effective distance, not only requested radius.

--version --json is shared by game/tools. Manifests include commit, dirty flag, compiler,
architecture, optimization, sanitizer, compiled SIMD and dependency baseline. Non-git archives
report unknown unless an explicit hexadecimal BLOXIDE_COMMIT_OVERRIDE is supplied.

112 PNGs are committed and must not change in v1.5. Regeneration is explicit. GLAD is also
vendored, generated with the pinned procedure in third_party/glad/UPSTREAM.md; Python/codegen
dependencies are not runtime/build requirements for normal consumers.

For formatting use clang-format 18.1.8, e.g. an official native Python installation plus
pip install clang-format==18.1.8. Run python .github/scripts/check_format.py. Do not format vendor.

## Install/package verification

cmake --install <build> --prefix <staging> produces bin executables/assets/shaders, lib Linux
runtimes, share/licenses/Bloxide notices and share/doc/Bloxide guides. CPack adds archive/checksum.
Linux package creation additionally requires patchelf. Windows MinGW packages require GCC runtime
exception/winpthreads notices, discovered relative to the selected compiler. Missing notices fail.

python .github/scripts/validate_package.py <archive> --gpu relocates to a Unicode path, removes
source-tree assumptions, checks missing/corrupt assets, runs native profiler and tests read-only
installation using ACL/chmod plus content hashes. It writes only to separate temporary output.
Windows hosted CI lacks a qualified GL driver, so the hardware package smoke is additionally
required locally; Ubuntu runs relocated GPU smoke under Mesa.

World metadata persists; edits/fluid journals do not. Existing metadata is never rewritten merely
because the executable/toolchain changes. New OS telemetry can be N/A without failing startup.
