include(GNUInstallDirs)
set(CMAKE_INSTALL_RPATH "$ORIGIN/../lib")
set_target_properties(${BLOXIDE_EXECUTABLES} PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")
install(TARGETS ${BLOXIDE_EXECUTABLES} RUNTIME DESTINATION bin)
install(DIRECTORY assets shaders DESTINATION bin)
install(FILES LICENSE THIRD_PARTY_NOTICES.md DESTINATION share/licenses/Bloxide)
install(DIRECTORY third_party/imgui/ DESTINATION share/licenses/Bloxide/imgui
    FILES_MATCHING PATTERN "LICENSE.txt" PATTERN "PINNED.md")
install(FILES third_party/imgui/imstb_rectpack.h third_party/imgui/imstb_textedit.h
    third_party/imgui/imstb_truetype.h third_party/imgui/imgui_draw.cpp
    DESTINATION share/licenses/Bloxide/imgui)
install(FILES third_party/lodepng/LICENSE third_party/lodepng/UPSTREAM.md DESTINATION share/licenses/Bloxide/lodepng)
install(DIRECTORY third_party/glad/licenses/ DESTINATION share/licenses/Bloxide/glad)
get_filename_component(compiler_prefix "${compiler_runtime_dir}" DIRECTORY)
find_file(BLOXIDE_SDL_LICENSE_FILE NAMES copyright LICENSE.txt COPYING.txt
    PATHS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/sdl2"
          "${compiler_prefix}/share/licenses/SDL2" "/usr/share/doc/libsdl2-2.0-0"
    NO_DEFAULT_PATH)
if(BLOXIDE_SDL_LICENSE_FILE)
    install(FILES "${BLOXIDE_SDL_LICENSE_FILE}" DESTINATION share/licenses/Bloxide/SDL2)
else()
    install(CODE "message(FATAL_ERROR \"SDL2 license missing: set BLOXIDE_SDL_LICENSE_FILE\")")
endif()
if(MINGW)
    foreach(package gcc-libs winpthreads)
        if(NOT IS_DIRECTORY "${compiler_prefix}/share/licenses/${package}")
            message(FATAL_ERROR "MinGW runtime notices missing: ${package}")
        endif()
        install(DIRECTORY "${compiler_prefix}/share/licenses/${package}/"
            DESTINATION "share/licenses/Bloxide/${package}")
    endforeach()
elseif(UNIX AND NOT APPLE)
    foreach(package libstdc++6 libgcc-s1)
        install(FILES "/usr/share/doc/${package}/copyright"
            DESTINATION "share/licenses/Bloxide/${package}")
    endforeach()
endif()
install(FILES README.md DEVELOPMENT.md CONTRIBUTING.md CODE_STYLE.md ARCHITECTURES.md PERFORMANCE.md
    DESTINATION share/doc/Bloxide)
install(CODE "
    set(root \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}\")
    file(GLOB programs \"\${root}/bin/Bloxide*${CMAKE_EXECUTABLE_SUFFIX}\")
    if(WIN32)
        file(GLOB dlls \"${CMAKE_BINARY_DIR}/*.dll\")
        file(INSTALL DESTINATION \"\${root}/bin\" TYPE FILE FILES \${dlls})
    elseif(UNIX AND NOT APPLE)
        file(GET_RUNTIME_DEPENDENCIES EXECUTABLES \${programs}
            RESOLVED_DEPENDENCIES_VAR dependencies UNRESOLVED_DEPENDENCIES_VAR missing
            PRE_EXCLUDE_REGEXES \"linux-vdso.*\"
            POST_EXCLUDE_REGEXES \".*/ld-linux.*\" \".*/lib(c|m|pthread|dl|rt|resolv)\\\\.so.*\"
                \".*/lib(GL|GLX|GLdispatch|OpenGL|EGL|X11|Xext|xcb|Xau|Xdmcp).*\")
        if(missing)
            message(FATAL_ERROR \"Unresolved package dependencies: \${missing}\")
        endif()
        foreach(dependency IN LISTS dependencies)
            file(INSTALL DESTINATION \"\${root}/lib\" TYPE SHARED_LIBRARY FOLLOW_SYMLINK_CHAIN FILES \"\${dependency}\")
        endforeach()
        find_program(patchelf patchelf REQUIRED)
        file(GLOB libraries \"\${root}/lib/*.so*\")
        foreach(library IN LISTS libraries)
            if(NOT IS_SYMLINK \"\${library}\")
                execute_process(COMMAND \"\${patchelf}\" --set-rpath \"$ORIGIN\" \"\${library}\"
                    COMMAND_ERROR_IS_FATAL ANY)
            endif()
        endforeach()
    endif()
    execute_process(COMMAND \"\${root}/bin/Bloxide${CMAKE_EXECUTABLE_SUFFIX}\" --version --json
        OUTPUT_VARIABLE identity RESULT_VARIABLE result OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR \"Installed executable cannot report its version\")
    endif()
    file(WRITE \"\${root}/build-manifest.json\" \"\${identity}\\n\")
")
set(CPACK_PACKAGE_NAME Bloxide)
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "Bloxide-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}-${CMAKE_CXX_COMPILER_ID}")
set(CPACK_PACKAGE_CHECKSUM SHA256)
if(WIN32)
    set(CPACK_GENERATOR ZIP)
else()
    set(CPACK_GENERATOR TGZ)
endif()
include(CPack)
