# Code style

Use C++17, UTF-8, LF and clang-format **18.1.8** with the root .clang-format.
Format only include/bloxide, src, tools and tests C/C++ files, including first-party .inl files.
Never reformat third_party, generated headers/loaders or binary assets.

Use RAII for resources, explicit ownership, small named interfaces and filename-specific errors.
Keep OpenGL on its owning thread; workers consume immutable snapshots and publish revision-checked
results. Do not change floating-point operation order during cleanup. Avoid broad mechanical commits
mixed with gameplay or algorithm changes.
