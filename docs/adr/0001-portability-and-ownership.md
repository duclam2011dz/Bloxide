# ADR 0001: v1.5 portability contract

Status: accepted.

- Pin vcpkg/SDL2; retain pinned ImGui/LodePNG. Driver OpenGL stays an OS dependency.
- Windows x64 GCC/MSVC and Ubuntu 22.04+ x64 GCC/Clang are release targets after qualification;
  macOS ARM64 is experimental. Linux uses X11/XWayland.
- GL creation/upload/destruction belongs to the main thread. Worker snapshots are immutable.
  Mesh publication is atomic across a revision-checked group; stale jobs cannot replace live data.
- GameApplication owns lifecycle and unwinds resources before destroying the GL context.
- Executables/assets/shaders install together under bin; Linux bundled runtimes under lib use
  relative RPATH. User data never belongs in the install prefix.
- Existing worldgen versions/metadata are not migrated. Reference GCC output stays exact.
  Determinism is guaranteed within qualified toolchain profiles, not promised across compilers/libm.
- Release requires headless, full Mesa GPU, ASan+UBSan, TSan and relocated-package gates.
  Mesa provides correctness evidence, not a hardware performance baseline.
- Dependency/asset provenance must be recorded and licenses included in packages.
