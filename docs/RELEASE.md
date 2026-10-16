# Release qualification (v1.5)

1. Require a clean candidate commit on `main`. Review all mandatory jobs in the
   Portability workflow: ten compiler/configuration builds, formatting, separate
   ASan+UBSan and TSan, the complete sequential Mesa GPU suite, and three relocated
   package validations. macOS ARM64 is experimental, not a Windows/Linux release gate.
2. Download `package-msvc`, `package-mingw`, and `package-gcc` from that exact run.
   Verify each SHA-256 sidecar before extraction. Never publish dirty local builds.
3. On the qualified Windows hardware/driver, validate both ZIPs with
   `.github/scripts/validate_package.py <archive> --gpu --expected-commit <sha>`.
   Run the five GPU fixtures against the extracted binaries, including LOD reversal
   and positive/negative floating-origin coordinates. Keep outputs/worlds outside the
   package, not in a player's default world. Record driver, commit and test outcomes.
4. Review `determinism-profile.json` artifacts across toolchains. Do not rewrite
   expected hashes to conceal differences. Preserve the Windows GCC reference.
5. Record measured baseline windows and qualification evidence in PERFORMANCE.md.
   A documentation-only candidate still needs its own clean CI/artifacts/identity check.
6. Only after mandatory CI and Windows hardware qualification pass, annotate and
   push `v1.5.0` at the qualified commit. Publish a GitHub Release with the two Windows
   ZIPs, Ubuntu-built TGZ, SHA-256 sidecars and notices. Package manifests must match
   that commit and report `dirty: false`. Source archives without Git metadata may
   report `unknown`; they must never guess a release commit.

Mesa establishes correctness, not hardware FPS. macOS does not receive an official
package. X11/XWayland and a working OpenGL 3.3 driver remain Linux prerequisites;
Windows packages do not include a GPU driver. Unsupported telemetry remains N/A.
