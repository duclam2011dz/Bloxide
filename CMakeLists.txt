cmake_minimum_required(VERSION 3.20)

project(Bloxide VERSION 1.4.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(SDL2 CONFIG REQUIRED)
find_package(OpenGL REQUIRED)
set(BLOXIDE_OPT_LEVEL "3" CACHE STRING "Release optimization level: 2 or 3")
if(NOT BLOXIDE_OPT_LEVEL MATCHES "^[23]$")
    message(FATAL_ERROR "BLOXIDE_OPT_LEVEL must be 2 or 3")
endif()
add_library(imgui STATIC third_party/imgui/imgui.cpp third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_widgets.cpp third_party/imgui/imgui_tables.cpp
    third_party/imgui/backends/imgui_impl_sdl2.cpp third_party/imgui/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui PUBLIC third_party/imgui third_party/imgui/backends)
target_link_libraries(imgui PUBLIC SDL2::SDL2 OpenGL::GL)

add_library(lodepng STATIC third_party/lodepng/lodepng.cpp)
target_include_directories(lodepng PUBLIC third_party/lodepng)
add_library(bloxide_assets STATIC src/Assets.cpp src/TextureGenerator.cpp)
target_include_directories(bloxide_assets PUBLIC include)
target_link_libraries(bloxide_assets PUBLIC lodepng)
if(MSVC)
    target_compile_options(bloxide_assets PRIVATE /W4 /permissive-)
else()
    target_compile_options(bloxide_assets PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_executable(BloxideTextureGenerator tools/TextureGenerator.cpp)
target_link_libraries(BloxideTextureGenerator PRIVATE bloxide_assets)

add_library(glad STATIC
    third_party/glad/src/glad.c
)
target_include_directories(glad PUBLIC third_party/glad/include)

add_library(bloxide_core STATIC
    src/Block.cpp
    src/Camera.cpp
    src/Chunk.cpp
    src/ChunkGenerator.cpp
    src/DensityWorld.cpp
    src/Heightmap.cpp
    src/SurfaceHierarchy.cpp
    src/ChunkMesher.cpp
    src/Profiler.cpp
    src/Trace.cpp
    src/Telemetry.cpp
    src/Physics.cpp
    src/Fluid.cpp
    src/Noise.cpp
    src/NoiseBatch.cpp
    src/NoiseSse2.cpp
    src/NoiseAvx2.cpp
    src/WorldSettings.cpp
    src/Targeting.cpp
    src/Renderer.cpp
    src/World.cpp
)
target_include_directories(bloxide_core PUBLIC include)
if(MSVC)
    set_source_files_properties(src/NoiseAvx2.cpp PROPERTIES COMPILE_OPTIONS "/arch:AVX2")
else()
    set_source_files_properties(src/NoiseSse2.cpp PROPERTIES COMPILE_OPTIONS "-msse2")
    set_source_files_properties(src/NoiseAvx2.cpp PROPERTIES COMPILE_OPTIONS "-mavx2;-mno-fma")
endif()
target_link_libraries(bloxide_core PUBLIC glad bloxide_assets)
if(WIN32)
    target_link_libraries(bloxide_core PUBLIC psapi pdh dxgi dxguid advapi32)
endif()
target_compile_definitions(bloxide_core PUBLIC SDL_MAIN_HANDLED)
target_compile_definitions(bloxide_core PUBLIC BLOXIDE_OPT_LEVEL_USED=${BLOXIDE_OPT_LEVEL})

if(MSVC)
    target_compile_options(bloxide_core PRIVATE /W4 /permissive- /fp:strict)
else()
    target_compile_options(bloxide_core PRIVATE -Wall -Wextra -Wpedantic -fno-fast-math -ffp-contract=off)
    target_compile_options(bloxide_core PRIVATE "$<$<CONFIG:Release>:-O${BLOXIDE_OPT_LEVEL}>")
endif()

add_executable(Bloxide
    src/Game.cpp
    src/main.cpp
)
add_library(bloxide_devtools STATIC src/DevTools.cpp)
target_include_directories(bloxide_devtools PUBLIC include)
target_link_libraries(bloxide_devtools PUBLIC bloxide_core imgui)
target_include_directories(Bloxide PRIVATE include)
target_compile_definitions(Bloxide PRIVATE
    SDL_MAIN_HANDLED
    BLOXIDE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
target_link_libraries(Bloxide PRIVATE bloxide_core bloxide_devtools SDL2::SDL2 OpenGL::GL)
add_custom_target(BloxideRuntimeAssets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:Bloxide>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/shaders" "$<TARGET_FILE_DIR:Bloxide>/shaders"
    VERBATIM)
add_dependencies(Bloxide BloxideRuntimeAssets)

add_executable(BloxideBenchmark
    tools/Benchmark.cpp
)
add_executable(BloxideNoiseBenchmark tools/NoiseBenchmark.cpp)
add_executable(BloxideNoiseLab tools/NoiseLab.cpp)
target_link_libraries(BloxideNoiseLab PRIVATE bloxide_core)
add_executable(BloxideProfiler tools/Profiler.cpp)
target_link_libraries(BloxideProfiler PRIVATE bloxide_core)
foreach(benchmark_target BloxideNoiseLab BloxideProfiler)
    target_compile_definitions(${benchmark_target} PRIVATE
        BLOXIDE_BENCHMARK_BUILD_TYPE="$<CONFIG>"
        "BLOXIDE_BENCHMARK_OPT_LEVEL=$<IF:$<CONFIG:Debug>,0,$<IF:$<CONFIG:Release>,${BLOXIDE_OPT_LEVEL},-1>>")
endforeach()
add_executable(BloxideTerrainBenchmark tools/TerrainBenchmark.cpp)
target_link_libraries(BloxideTerrainBenchmark PRIVATE bloxide_core)
target_link_libraries(BloxideNoiseBenchmark PRIVATE bloxide_core)
target_include_directories(BloxideBenchmark PRIVATE include)
target_link_libraries(BloxideBenchmark PRIVATE bloxide_core)

if(WIN32)
    add_custom_command(TARGET Bloxide POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_PREFIX_PATH}/bin/SDL2.dll"
            "$<TARGET_FILE_DIR:Bloxide>/SDL2.dll"
        VERBATIM
    )
endif()

include(CTest)
option(BLOXIDE_RENDER_TESTS "Run SDL/OpenGL framebuffer regression test (requires GPU driver)" OFF)
if(BUILD_TESTING)
    add_executable(BloxideWorldTests
        tests/WorldTests.cpp
    )
    target_include_directories(BloxideWorldTests PRIVATE include)
    target_link_libraries(BloxideWorldTests PRIVATE bloxide_core)
    add_test(NAME BloxideWorldTests COMMAND BloxideWorldTests)
    add_test(NAME BloxideBenchmarkSmoke COMMAND BloxideBenchmark --quick)
    add_test(NAME BloxideSeedReproducibility COMMAND ${CMAKE_COMMAND} "-DPROBE=$<TARGET_FILE:BloxideNoiseBenchmark>" -P "${CMAKE_SOURCE_DIR}/tests/Reproducibility.cmake")
    set_tests_properties(BloxideSeedReproducibility PROPERTIES TIMEOUT 90)
    add_test(NAME BloxideFieldsReproducibility COMMAND ${CMAKE_COMMAND} "-DPROBE=$<TARGET_FILE:BloxideNoiseBenchmark>" "-DEXTRA=--fields" -P "${CMAKE_SOURCE_DIR}/tests/Reproducibility.cmake")
    set_tests_properties(BloxideFieldsReproducibility PROPERTIES TIMEOUT 90)
    add_test(NAME BloxideDensityReproducibility COMMAND ${CMAKE_COMMAND} "-DPROBE=$<TARGET_FILE:BloxideNoiseBenchmark>" "-DEXTRA=--density" -P "${CMAKE_SOURCE_DIR}/tests/Reproducibility.cmake")
    set_tests_properties(BloxideDensityReproducibility PROPERTIES TIMEOUT 120)
    add_executable(BloxideRegressionTests tests/RegressionTests.cpp)
    target_link_libraries(BloxideRegressionTests PRIVATE bloxide_core)
    add_test(NAME BloxideRegressionTests COMMAND BloxideRegressionTests)
    add_executable(BloxideV120Tests tests/V120Tests.cpp)
    target_link_libraries(BloxideV120Tests PRIVATE bloxide_core)
    add_test(NAME BloxideV120Tests COMMAND BloxideV120Tests)
    add_executable(BloxideV125Tests tests/V125Tests.cpp)
    target_link_libraries(BloxideV125Tests PRIVATE bloxide_core)
    target_compile_definitions(BloxideV125Tests PRIVATE BLOXIDE_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets/textures")
    add_test(NAME BloxideV125Tests COMMAND BloxideV125Tests)
    add_executable(BloxideV130Tests tests/V130Tests.cpp)
    add_executable(BloxideV135Tests tests/V135Tests.cpp)
    add_executable(BloxideV140CoreTests tests/V140CoreTests.cpp)
    target_link_libraries(BloxideV140CoreTests PRIVATE bloxide_core)
    add_test(NAME BloxideV140CoreTests COMMAND BloxideV140CoreTests)
    set_tests_properties(BloxideV140CoreTests PROPERTIES TIMEOUT 120)
    target_link_libraries(BloxideV135Tests PRIVATE bloxide_core)
    add_test(NAME BloxideV135Tests COMMAND BloxideV135Tests)
    set_tests_properties(BloxideV135Tests PROPERTIES TIMEOUT 120)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set_tests_properties(BloxideV135Tests PROPERTIES TIMEOUT 300)
    endif()
    target_link_libraries(BloxideV130Tests PRIVATE bloxide_core)
    add_test(NAME BloxideV130Tests COMMAND BloxideV130Tests)
    set_tests_properties(BloxideV130Tests PROPERTIES TIMEOUT 90)
    set_tests_properties(BloxideV125Tests PROPERTIES TIMEOUT 90)
    set_tests_properties(BloxideV120Tests PROPERTIES TIMEOUT 90)
    set_tests_properties(BloxideRegressionTests PROPERTIES TIMEOUT 45)
    if(BLOXIDE_RENDER_TESTS)
        add_test(NAME BloxideDensityRenderRegression COMMAND Bloxide --smoke-test --no-lod --world "${CMAKE_BINARY_DIR}/testing/density-gl" --preset mountainous --seed 130)
        set_tests_properties(BloxideDensityRenderRegression PROPERTIES TIMEOUT 120)
        set(BLOXIDE_TEST_FAR_RADIUS 24)
        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            set(BLOXIDE_TEST_FAR_RADIUS 12)
        endif()
        add_test(NAME BloxideLodRenderRegression COMMAND Bloxide --smoke-test --require-lod --world "${CMAKE_BINARY_DIR}/testing/density-lod-gl" --far-radius ${BLOXIDE_TEST_FAR_RADIUS} --preset mountainous --seed 130)
        set_tests_properties(BloxideLodRenderRegression PROPERTIES TIMEOUT 240)
        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            set_tests_properties(BloxideLodRenderRegression PROPERTIES TIMEOUT 300)
        endif()
        add_test(NAME BloxideRenderRegression COMMAND Bloxide --smoke-test)
        add_test(NAME BloxideFarRenderPositive COMMAND Bloxide --smoke-test --smoke-origin 1099511627648)
        add_test(NAME BloxideFarRenderNegative COMMAND Bloxide --smoke-test --smoke-origin -1099511627648)
        set_tests_properties(BloxideFarRenderPositive BloxideFarRenderNegative PROPERTIES TIMEOUT 90)
        set_tests_properties(BloxideRenderRegression PROPERTIES TIMEOUT 90)
    endif()
    if(MSVC)
        target_compile_options(BloxideWorldTests PRIVATE /UNDEBUG)
    else()
        target_compile_options(BloxideWorldTests PRIVATE -UNDEBUG)
    endif()
endif()
