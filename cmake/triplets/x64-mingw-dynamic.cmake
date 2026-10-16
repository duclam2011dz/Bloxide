set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)
set(VCPKG_CMAKE_SYSTEM_NAME MinGW)
set(VCPKG_POLICY_DLLS_WITHOUT_LIBS enabled)
set(VCPKG_ENV_PASSTHROUGH PATH)
# pkg-config is a host metadata checker, not a target binary dependency.
# Respect an explicit host tool instead of requiring a particular MSYS mirror.
set(VCPKG_ENV_PASSTHROUGH_UNTRACKED PKG_CONFIG)
