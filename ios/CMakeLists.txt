# iOS build of the native game (Xcode generator, CMAKE_SYSTEM_NAME=iOS); see scripts/build_ios.sh.
# rich4_core and tools/ sources come straight from the repository root; third-party sources from
# ios/third_party/ (downloaded and checksum-verified by scripts/build_ios.sh, same pins as Android).
cmake_minimum_required(VERSION 3.24)
project(rich4_ios C CXX OBJC OBJCXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  message(FATAL_ERROR "Configure with -G Xcode -DCMAKE_SYSTEM_NAME=iOS (use scripts/build_ios.sh)")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(RICH4_REPO "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(THIRD_PARTY "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
if(NOT EXISTS "${THIRD_PARTY}/SDL3-3.4.16/CMakeLists.txt")
  message(FATAL_ERROR "Third-party sources missing; run scripts/build_ios.sh first")
endif()
set(RICH4_GAME_ASSETS "${RICH4_REPO}/game-assets" CACHE PATH "Original game files copied into the app")
set(RICH4_FONT "${THIRD_PARTY}/downloads/NotoSerifCJKtc-Regular.otf" CACHE FILEPATH "Bundled CJK font")
set(RICH4_ASSET_CATALOG "" CACHE PATH "Generated Assets.xcassets with AppIcon (scripts/make_ios_icon.py)")

# Signing: simulator builds are ad-hoc signed; device builds sign only the app, with Automatic
# signing for -DRICH4_IOS_TEAM=<team ID> (scripts/build_ios.sh passes $RICH4_IOS_TEAM). The script
# also configures device builds with CMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO so CMake's own
# probe projects and the static libraries never try to sign (that hangs without a team).
set(RICH4_IOS_TEAM "" CACHE STRING "Apple development team ID for device builds")
if(CMAKE_OSX_SYSROOT MATCHES "simulator")
  set(RICH4_SIGN_IDENTITY "-")
  set(RICH4_SIGN_STYLE Manual)
else()
  set(RICH4_SIGN_IDENTITY "Apple Development")
  set(RICH4_SIGN_STYLE Automatic)
endif()

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
# iOS defaults every executable to an app bundle; only the game is one (dependencies declare
# helper executables whose install() rules reject bundles).
set(CMAKE_MACOSX_BUNDLE OFF)

# SDL3, static (an iOS app is a single executable; SDL_main.h supplies UIApplicationMain).
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory("${THIRD_PARTY}/SDL3-3.4.16" SDL3 EXCLUDE_FROM_ALL)

# FreeType without optional dependencies.
foreach(dependency ZLIB BZIP2 PNG HARFBUZZ BROTLI)
  set(FT_DISABLE_${dependency} ON CACHE BOOL "" FORCE)
endforeach()
add_subdirectory("${THIRD_PARTY}/freetype-2.14.3" freetype EXCLUDE_FROM_ALL)

# Portable game core from the repository root (tests and desktop tools excluded).
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(RICH4_BUILD_VIEWER OFF CACHE BOOL "" FORCE)
set(RICH4_BUILD_GAME OFF CACHE BOOL "" FORCE)
add_subdirectory("${RICH4_REPO}" rich4 EXCLUDE_FROM_ALL)

set(sources
  "${RICH4_REPO}/tools/native_game.cpp"
  "${RICH4_REPO}/tools/native_audio.cpp"
  "${RICH4_REPO}/tools/screens/main_screen.cpp"
  "${RICH4_REPO}/tools/screens/draw.cpp"
  "${RICH4_REPO}/tools/screens/finance.cpp"
  "${RICH4_REPO}/tools/screens/stocks.cpp"
  "${RICH4_REPO}/tools/screens/market.cpp"
  "${RICH4_REPO}/tools/screens/menus.cpp"
  "${RICH4_REPO}/tools/screens/effects.cpp"
  "${RICH4_REPO}/tools/screens/intro.cpp"
  "${RICH4_REPO}/tools/screens/messages.cpp"
  "${RICH4_REPO}/tools/screens/items.cpp"
  "${RICH4_REPO}/tools/screens/spinners.cpp"
  "${RICH4_REPO}/tools/screens/facility.cpp"
  "${RICH4_REPO}/tools/screens/monthly.cpp"
  "${RICH4_REPO}/tools/screens/endgame.cpp"
  "${RICH4_REPO}/tools/screens/help.cpp"
  "${RICH4_REPO}/tools/screens/minigames.cpp"
  "${RICH4_REPO}/tools/screens/tile_popup.cpp"
  "${RICH4_REPO}/tools/screens/ai_settings.cpp"
  rich4_ios.mm)
if(RICH4_ASSET_CATALOG)
  list(APPEND sources "${RICH4_ASSET_CATALOG}")
  set_source_files_properties("${RICH4_ASSET_CATALOG}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
endif()

add_executable(rich4 MACOSX_BUNDLE ${sources})
target_include_directories(rich4 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${RICH4_REPO}/tools")
target_link_libraries(rich4 PRIVATE rich4_core rich4_tinysoundfont rich4_stb_vorbis SDL3::SDL3-static freetype iconv
  "-framework UIKit")
set_source_files_properties(rich4_ios.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
set_target_properties(rich4 PROPERTIES
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
  XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER io.divby0.rich4
  XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2" # iPhone and iPad
  XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME AppIcon
  XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "${RICH4_SIGN_IDENTITY}"
  XCODE_ATTRIBUTE_CODE_SIGN_STYLE ${RICH4_SIGN_STYLE}
  XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED YES
  XCODE_ATTRIBUTE_ENABLE_BITCODE NO
  XCODE_ATTRIBUTE_GCC_OPTIMIZATION_LEVEL[variant=Debug] s # the game renders on the CPU every frame
  XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT dwarf)
if(RICH4_IOS_TEAM)
  set_target_properties(rich4 PROPERTIES XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${RICH4_IOS_TEAM}")
endif()

# Game files (never committed), font and soundfont go into the bundle on every build, before
# Xcode signs it. Saves live outside the bundle (SDL_GetPrefPath).
add_custom_command(TARGET rich4 POST_BUILD
  COMMAND "${CMAKE_COMMAND}" "-DGAME_ASSETS=${RICH4_GAME_ASSETS}" "-DFONT=${RICH4_FONT}"
    "-DSOUNDFONT=${THIRD_PARTY}/downloads/TimGM6mb.sf2"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/copy_resources.cmake"
  VERBATIM)

# Dev only: real-touch UI test (ios/uitests); see README. Off in scripts/build_ios.sh builds.
option(RICH4_IOS_UITESTS "Add the XCUITest touch walkthrough target" OFF)
if(RICH4_IOS_UITESTS)
  set_target_properties(rich4 PROPERTIES XCODE_GENERATE_SCHEME ON) # scheme "rich4" runs the test
  add_library(rich4_touch_tests MODULE uitests/Rich4TouchTests.m)
  set_target_properties(rich4_touch_tests PROPERTIES
    BUNDLE TRUE BUNDLE_EXTENSION xctest XCTEST TRUE XCTEST_TESTEE rich4
    XCODE_PRODUCT_TYPE com.apple.product-type.bundle.ui-testing
    XCODE_ATTRIBUTE_TEST_TARGET_NAME rich4
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER io.divby0.rich4.touchtests
    XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "${RICH4_SIGN_IDENTITY}"
    XCODE_ATTRIBUTE_CODE_SIGN_STYLE ${RICH4_SIGN_STYLE}
    XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES)
  target_link_libraries(rich4_touch_tests PRIVATE "-framework XCTest" "-framework UIKit")
  add_dependencies(rich4_touch_tests rich4)
endif()
