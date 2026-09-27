cmake_minimum_required(VERSION 3.20)

project(Bloxide VERSION 1.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(SDL2 CONFIG REQUIRED)
find_package(OpenGL REQUIRED)

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
    src/Renderer.cpp
    src/World.cpp
)
target_include_directories(bloxide_core PUBLIC include)
target_link_libraries(bloxide_core PUBLIC glad)
target_compile_definitions(bloxide_core PUBLIC SDL_MAIN_HANDLED)

if(MSVC)
    target_compile_options(bloxide_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(bloxide_core PRIVATE -Wall -Wextra -Wpedantic)
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

add_executable(BloxideBenchmark
    tools/Benchmark.cpp
)
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
if(BUILD_TESTING)
    add_executable(BloxideWorldTests
        tests/WorldTests.cpp
    )
    target_include_directories(BloxideWorldTests PRIVATE include)
    target_link_libraries(BloxideWorldTests PRIVATE bloxide_core)
    add_test(NAME BloxideWorldTests COMMAND BloxideWorldTests)
    add_test(NAME BloxideBenchmarkSmoke COMMAND BloxideBenchmark --quick)
endif()
