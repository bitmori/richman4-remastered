# Android build of the native game: rich4_core and tools/ sources come straight
# from the repository root; third-party sources from android/third_party/
# (downloaded and checksum-verified by scripts/build_android.sh).
cmake_minimum_required(VERSION 3.22)
project(rich4_android C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
get_filename_component(RICH4_REPO "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
set(THIRD_PARTY "${RICH4_REPO}/android/third_party")
if(NOT EXISTS "${THIRD_PARTY}/SDL3-3.4.16/CMakeLists.txt")
  message(FATAL_ERROR "Third-party sources missing; run scripts/build_android.sh first")
endif()

# SDL3 (shared: SDLActivity loads libSDL3.so then libmain.so).
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory("${THIRD_PARTY}/SDL3-3.4.16" SDL3)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# FreeType without optional dependencies.
foreach(dependency ZLIB BZIP2 PNG HARFBUZZ BROTLI)
  set(FT_DISABLE_${dependency} ON CACHE BOOL "" FORCE)
endforeach()
add_subdirectory("${THIRD_PARTY}/freetype-2.14.3" freetype EXCLUDE_FROM_ALL)

# FluidSynth 2.6 with the C++11 OS abstraction: no glib, every optional backend off.
# The game renders MIDI itself into an SDL audio stream, so no FluidSynth driver is needed.
set(osal cpp11 CACHE STRING "" FORCE)
foreach(feature alsa aufile dbus ipv6 jack ladspa libsndfile midishare network oss sdl3 pulseaudio
        pipewire readline openmp opensles oboe signalsmith systemd portaudio)
  set(enable-${feature} OFF CACHE BOOL "" FORCE)
endforeach()
add_subdirectory("${THIRD_PARTY}/fluidsynth-2.6.1" fluidsynth EXCLUDE_FROM_ALL)

# GNU libiconv: bionic's iconv only knows UTF/ASCII/Latin-1, but the game decodes Big5.
# Its iconv.h maps iconv_open/iconv/iconv_close to libiconv_* so it never clashes with bionic.
include(ExternalProject)
set(ICONV_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/libiconv-install")
set(ICONV_CC "${ANDROID_TOOLCHAIN_ROOT}/bin/aarch64-linux-android${ANDROID_PLATFORM_LEVEL}-clang")
ExternalProject_Add(libiconv_build
  SOURCE_DIR "${THIRD_PARTY}/libiconv-1.18"
  BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/libiconv-build"
  CONFIGURE_COMMAND "${THIRD_PARTY}/libiconv-1.18/configure" --host=aarch64-linux-android
    --prefix=${ICONV_PREFIX} --enable-static --disable-shared --disable-nls --with-pic
    CC=${ICONV_CC} AR=${CMAKE_AR} RANLIB=${CMAKE_RANLIB} CFLAGS=-O2
  BUILD_COMMAND make lib/localcharset.h COMMAND make -j8 -C lib
  INSTALL_COMMAND make install-lib libdir=${ICONV_PREFIX}/lib includedir=${ICONV_PREFIX}/include
  BUILD_BYPRODUCTS "${ICONV_PREFIX}/lib/libiconv.a")

# Portable game core from the repository root (tests and desktop tools excluded).
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(RICH4_BUILD_VIEWER OFF CACHE BOOL "" FORCE)
set(RICH4_BUILD_GAME OFF CACHE BOOL "" FORCE)
add_subdirectory("${RICH4_REPO}" rich4 EXCLUDE_FROM_ALL)

add_library(main SHARED
  "${RICH4_REPO}/tools/native_game.cpp"
  "${RICH4_REPO}/tools/native_audio.cpp"
  "${RICH4_REPO}/tools/screens/effects.cpp"
  rich4_android.cpp)
add_dependencies(main libiconv_build)
target_include_directories(main PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${ICONV_PREFIX}/include")
target_link_libraries(main PRIVATE rich4_core SDL3::SDL3 freetype libfluidsynth
  "${ICONV_PREFIX}/lib/libiconv.a" android log)
