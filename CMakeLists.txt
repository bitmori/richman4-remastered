cmake_minimum_required(VERSION 3.22)
project(rich4_native VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(RICH4_SANITIZERS "Check memory safety and undefined behavior" OFF)

add_library(rich4_core src/assets.cpp)
target_include_directories(rich4_core PUBLIC include)
target_compile_options(rich4_core PRIVATE
  $<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wsign-conversion>)
if(RICH4_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(rich4_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(rich4_core PUBLIC -fsanitize=address,undefined)
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(assets_tests tests/assets_tests.cpp)
  target_link_libraries(assets_tests PRIVATE rich4_core)
  add_test(NAME assets COMMAND assets_tests)
endif()
