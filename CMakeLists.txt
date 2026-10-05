cmake_minimum_required(VERSION 3.22)
project(rich4_native VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(RICH4_BUILD_VIEWER "Build the SDL3 resource inspection application" ON)
option(RICH4_SANITIZERS "Check memory safety and undefined behavior" OFF)

add_library(rich4_core src/assets.cpp src/graphics.cpp src/save.cpp src/calendar.cpp src/direction.cpp
  src/inventory.cpp src/economy.cpp)
target_include_directories(rich4_core PUBLIC include)
target_compile_options(rich4_core PRIVATE
  $<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wsign-conversion>)
if(RICH4_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(rich4_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(rich4_core PUBLIC -fsanitize=address,undefined)
endif()

add_executable(rich4-tool tools/rich4_tool.cpp)
target_link_libraries(rich4-tool PRIVATE rich4_core)

if(RICH4_BUILD_VIEWER)
  find_package(SDL3 3.2 CONFIG REQUIRED)
  add_executable(rich4-resource-viewer tools/resource_viewer.cpp)
  target_link_libraries(rich4-resource-viewer PRIVATE rich4_core SDL3::SDL3)
  if(APPLE)
    set_target_properties(rich4-resource-viewer PROPERTIES
      MACOSX_BUNDLE TRUE
      MACOSX_BUNDLE_GUI_IDENTIFIER org.rich4.ResourceViewer
      MACOSX_BUNDLE_BUNDLE_NAME "Rich4 Resource Viewer")
  endif()
endif()

include(CTest)
if(BUILD_TESTING)
  foreach(module assets graphics save calendar direction inventory economy)
    add_executable(${module}_tests tests/${module}_tests.cpp)
    target_link_libraries(${module}_tests PRIVATE rich4_core)
    add_test(NAME ${module} COMMAND ${module}_tests)
  endforeach()
endif()
