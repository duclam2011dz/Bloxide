cmake_minimum_required(VERSION 3.20)

project(Bloxide VERSION 1.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(SDL2 CONFIG REQUIRED)
find_package(OpenGL REQUIRED)

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
    src/ChunkMesher.cpp
    src/Profiler.cpp
    src/Physics.cpp
    src/Noise.cpp
    src/WorldSettings.cpp
    src/Targeting.cpp
    src/Renderer.cpp
    src/World.cpp
)
target_include_directories(bloxide_core PUBLIC include)
target_link_libraries(bloxide_core PUBLIC glad bloxide_assets)
target_compile_definitions(bloxide_core PUBLIC SDL_MAIN_HANDLED)

if(MSVC)
    target_compile_options(bloxide_core PRIVATE /W4 /permissive- /fp:strict)
else()
    target_compile_options(bloxide_core PRIVATE -Wall -Wextra -Wpedantic -fno-fast-math -ffp-contract=off)
endif()

add_executable(Bloxide
    src/Game.cpp
    src/main.cpp
)
target_include_directories(Bloxide PRIVATE include)
target_compile_definitions(Bloxide PRIVATE
    SDL_MAIN_HANDLED
    BLOXIDE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
target_link_libraries(Bloxide PRIVATE bloxide_core SDL2::SDL2 OpenGL::GL)
add_custom_target(BloxideRuntimeAssets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:Bloxide>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/shaders" "$<TARGET_FILE_DIR:Bloxide>/shaders"
    VERBATIM)
add_dependencies(Bloxide BloxideRuntimeAssets)

add_executable(BloxideBenchmark
    tools/Benchmark.cpp
)
add_executable(BloxideNoiseBenchmark tools/NoiseBenchmark.cpp)
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
    target_link_libraries(BloxideV130Tests PRIVATE bloxide_core)
    add_test(NAME BloxideV130Tests COMMAND BloxideV130Tests)
    set_tests_properties(BloxideV130Tests PROPERTIES TIMEOUT 90)
    set_tests_properties(BloxideV125Tests PROPERTIES TIMEOUT 90)
    set_tests_properties(BloxideV120Tests PROPERTIES TIMEOUT 90)
    set_tests_properties(BloxideRegressionTests PROPERTIES TIMEOUT 45)
    if(BLOXIDE_RENDER_TESTS)
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
