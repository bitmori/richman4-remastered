cmake_minimum_required(VERSION 3.22)
project(rich4_native VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(RICH4_BUILD_VIEWER "Build the SDL3 resource inspection application" ON)
option(RICH4_BUILD_GAME "Build the native SDL3 game" ON)
option(RICH4_SANITIZERS "Check memory safety and undefined behavior" OFF)
set(RICH4_ORACLE_DIR "" CACHE PATH "Optional original EXE capture for differential tests")

add_library(rich4_core src/assets.cpp src/graphics.cpp src/save.cpp src/calendar.cpp src/direction.cpp
  src/inventory.cpp src/economy.cpp src/random.cpp src/terrain.cpp src/board_renderer.cpp src/world.cpp src/game.cpp src/events.cpp
  src/stocks.cpp src/services.cpp src/auction.cpp src/tools.cpp src/lottery.cpp src/holidays.cpp src/cards.cpp src/cards_ai.cpp src/gods.cpp src/tools_ai.cpp src/special_actors.cpp src/stocks_ai.cpp
  src/trading.cpp src/world_validation.cpp)
target_include_directories(rich4_core PUBLIC include)
target_compile_options(rich4_core PRIVATE
  $<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wsign-conversion>)
if(RICH4_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(rich4_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(rich4_core PUBLIC -fsanitize=address,undefined)
endif()

add_executable(rich4-tool tools/rich4_tool.cpp)
target_link_libraries(rich4-tool PRIVATE rich4_core)
add_executable(rich4-state-check tools/playability/native_state_check.cpp)
target_link_libraries(rich4-state-check PRIVATE rich4_core)
add_executable(rich4-native-fixture tools/playability/native_fixture.cpp)
target_link_libraries(rich4-native-fixture PRIVATE rich4_core)

if(RICH4_BUILD_VIEWER OR RICH4_BUILD_GAME)
  find_package(SDL3 3.2 CONFIG REQUIRED)
endif()
if(RICH4_BUILD_VIEWER)
  add_executable(rich4-resource-viewer tools/resource_viewer.cpp)
  target_link_libraries(rich4-resource-viewer PRIVATE rich4_core SDL3::SDL3)
  if(APPLE)
    set_target_properties(rich4-resource-viewer PROPERTIES
      MACOSX_BUNDLE TRUE
      MACOSX_BUNDLE_GUI_IDENTIFIER org.rich4.ResourceViewer
      MACOSX_BUNDLE_BUNDLE_NAME "Rich4 Resource Viewer")
  endif()
endif()

if(RICH4_BUILD_GAME)
  find_package(Freetype REQUIRED)
  find_package(Iconv REQUIRED)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(FLUIDSYNTH REQUIRED IMPORTED_TARGET fluidsynth)
  add_executable(rich4-native tools/native_game.cpp tools/native_audio.cpp)
  target_link_libraries(rich4-native PRIVATE rich4_core SDL3::SDL3 Freetype::Freetype Iconv::Iconv PkgConfig::FLUIDSYNTH)
  target_compile_options(rich4-native PRIVATE $<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-Wall;-Wextra;-Wpedantic>)
  if(APPLE)
    set_target_properties(rich4-native PROPERTIES MACOSX_BUNDLE TRUE
      MACOSX_BUNDLE_GUI_IDENTIFIER org.rich4.Native MACOSX_BUNDLE_BUNDLE_NAME "大富翁 4")
  endif()
endif()

include(CTest)
if(BUILD_TESTING)
  if(RICH4_BUILD_GAME)
    add_executable(native_audio_tests tests/native_audio_tests.cpp tools/native_audio.cpp)
    target_include_directories(native_audio_tests PRIVATE tools)
    target_link_libraries(native_audio_tests PRIVATE rich4_core SDL3::SDL3 PkgConfig::FLUIDSYNTH)
    add_test(NAME native_audio COMMAND native_audio_tests)
  endif()
  add_executable(rich4-graphics-oracle tools/oracle/native_graphics.cpp)
  target_link_libraries(rich4-graphics-oracle PRIVATE rich4_core)
  foreach(module assets graphics save calendar direction inventory economy random terrain board_renderer world game services auction events stocks tools lottery holidays cards cards_ai numeric gods tools_ai special_actors stocks_ai trading world_validation)
    add_executable(${module}_tests tests/${module}_tests.cpp)
    target_link_libraries(${module}_tests PRIVATE rich4_core)
    add_test(NAME ${module} COMMAND ${module}_tests)
  endforeach()
  find_package(Python3 3.9 COMPONENTS Interpreter)
  if(Python3_Interpreter_FOUND)
    add_test(NAME oracle_manifest_tests
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/oracle_manifest_tests.py)
    add_test(NAME playability_state_tests
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/playability_state_tests.py
        $<TARGET_FILE:rich4-state-check>)
    add_test(NAME playability_runner_tests
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/playability_runner_tests.py)
    add_test(NAME native_playability_tests
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/native_playability_tests.py)
    add_test(NAME native_fixture_tests
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/native_fixture_tests.py $<TARGET_FILE:rich4-native-fixture>)
    add_test(NAME native_acceptance_tests
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/native_acceptance_tests.py)
  endif()
  if(RICH4_ORACLE_DIR)
    if(NOT Python3_Interpreter_FOUND)
      message(FATAL_ERROR "Original EXE verification requires Python 3.9 or newer")
    endif()
    get_filename_component(oracle_directory "${RICH4_ORACLE_DIR}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    set(golden "${oracle_directory}/golden")
    add_test(NAME original_manifest COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/scripts/oracle_manifest.py "${oracle_directory}")
    set_tests_properties(original_manifest PROPERTIES FIXTURES_SETUP original_capture)
    add_test(NAME original_graphics COMMAND rich4-graphics-oracle "${golden}")
    add_test(NAME original_calendar COMMAND calendar_tests "${golden}/calendar.csv" "${golden}/calendar_packed.csv")
    add_test(NAME original_direction COMMAND direction_tests "${golden}/direction.csv")
    add_test(NAME original_inventory COMMAND inventory_tests "${golden}/inventory.txt")
    add_test(NAME original_economy COMMAND economy_tests "${golden}/economy.txt")
    add_test(NAME original_random COMMAND random_tests "${golden}/random.csv")
    add_test(NAME original_terrain COMMAND terrain_tests "${golden}/terrain.jsonl")
    set_tests_properties(original_graphics original_calendar original_direction original_inventory
      original_economy original_random original_terrain PROPERTIES FIXTURES_REQUIRED original_capture)
  endif()
endif()
