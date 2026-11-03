# Native build for the Android frontend.
#
# The emulator core (src/*.c) is plain C99 and platform-independent, so it is
# compiled as-is. acore.c wraps it with the settings/ROM/state/audio glue and
# jni_bridge.c exposes it to Java. The desktop frontends (main.c, win32.c,
# gui.c, video.c, vk.c, export.c, fb.c) are NOT part of this build.
cmake_minimum_required(VERSION 3.22.1)
project(boycottadvance_reborn C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Root of the repository, relative to this file (android/app/src/main/cpp).
set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../)

set(CORE_SRC
    ${REPO_ROOT}/src/gba.c
    ${REPO_ROOT}/src/mem.c
    ${REPO_ROOT}/src/arm.c
    ${REPO_ROOT}/src/ppu.c
    ${REPO_ROOT}/src/apu.c
    ${REPO_ROOT}/src/bios.c
    ${REPO_ROOT}/src/eeprom.c
    ${REPO_ROOT}/src/flash.c
    ${REPO_ROOT}/src/rtc.c)

add_library(boycottadvance SHARED
    ${CORE_SRC}
    ${CMAKE_CURRENT_SOURCE_DIR}/acore.c
    ${CMAKE_CURRENT_SOURCE_DIR}/jni_bridge.c)

target_include_directories(boycottadvance PRIVATE
    ${REPO_ROOT}/src
    ${CMAKE_CURRENT_SOURCE_DIR})

# Gradle builds the APK with the CMake "Debug" build type, whose default flags
# are just -g (no optimization). At -O0 the interpreter cannot reach real time
# on a phone (measured ~1.06x real time at -O0 vs ~3.9x at -O2 on a desktop
# CPU), so the emulation falls behind: the audio device starves (crackle) and
# frame pacing stutters. Force the same -O2 the desktop builds use.
target_compile_options(boycottadvance PRIVATE -O2)

# Android logging + the system libraries the frontend uses.
find_library(log-lib log)
find_library(android-lib android)
find_library(EGL-lib EGL)
find_library(GLES-lib GLESv2)
find_library(openSL-lib OpenSLES)
target_link_libraries(boycottadvance
    ${log-lib}
    ${android-lib}
    ${EGL-lib}
    ${GLES-lib}
    ${openSL-lib}
    m)
