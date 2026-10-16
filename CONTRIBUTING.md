# Contributing

Start with DEVELOPMENT.md and CODE_STYLE.md. Keep gameplay/worldgen changes separate from portability work.
Never commit build directories, user worlds, benchmark captures, credentials or generated packages.

Before submitting, run the selected configure/build/test presets, first-party clang-format check and
package validation. PR CI also runs complete Mesa GPU regressions, ASan+UBSan and TSan; do not disable
these gates or refresh goldens simply to obtain a green build.

Keep dependency revisions/licenses and ADRs current. Explain numerical differences with a reviewed
determinism profile; preserve the reference Windows GCC output. Record the compiler/OS/architecture,
configuration and actual test results. Software-rendered FPS is not hardware performance evidence.
