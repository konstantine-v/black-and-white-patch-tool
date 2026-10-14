cmake_minimum_required(VERSION 3.25)
project(BlackWhiteDecomp VERSION 0.1.0 LANGUAGES CXX)

if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Build with MSVC for 32-bit x86 (-A Win32): the code runs inside the 32-bit runblack.exe.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Static CRT: nothing to install next to the game. The game's own CRT and heap are separate (see core/GameHeap.h).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(bw_options INTERFACE)
target_compile_options(bw_options INTERFACE
    /W4 /WX /permissive- /utf-8 /Zc:preprocessor /Zc:__cplusplus /EHsc /arch:SSE2)
target_compile_definitions(bw_options INTERFACE
    _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE)
target_include_directories(bw_options INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_BINARY_DIR}/generated)

# The decompilation's own version (CHANGELOG.md), logged at start-up and shown by the launcher.
configure_file(src/core/Version.h.in generated/core/Version.h @ONLY)

# Infrastructure shared by the DLL, the launcher and the tests.
add_library(bw_core STATIC
    src/core/Config.cpp
    src/core/GameHeap.cpp
    src/core/Hook.cpp
    src/core/Image.cpp
    src/core/Log.cpp
    src/core/Patch.cpp)
target_link_libraries(bw_core PUBLIC bw_options bcrypt)

# Decompiled game code and fixes. An OBJECT library so every translation unit (and its hook registrations) is linked
# even though nothing references it by name.
add_library(bw_game OBJECT
    src/game/black/CPUCheck.cpp
    src/game/lhlib/LHScreen.cpp
    src/fixes/Fixes.cpp)
target_link_libraries(bw_game PUBLIC bw_core ddraw dxguid)

add_library(BWDecomp SHARED src/dll/DllMain.cpp)
target_link_libraries(BWDecomp PRIVATE bw_game)

add_executable(BWLaunch WIN32 src/launcher/Launcher.cpp)
target_link_libraries(BWLaunch PRIVATE bw_core shell32)

enable_testing()
add_executable(bw_tests tests/TestMain.cpp tests/CoreTests.cpp tests/GameTests.cpp)
target_link_libraries(bw_tests PRIVATE bw_game)
add_test(NAME unit COMMAND bw_tests)
