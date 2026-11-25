# Build your own std::vector - test harness.
#
#   cmake --preset debug && cmake --build --preset debug && ctest --preset debug
#   ctest --preset debug -L stage-02            # one stage
#
# Options:
#   USE_STD=ON            test std::vector instead (proves the tests are fair)
#   CPPATLAS_SANITIZE=ON  AddressSanitizer + UndefinedBehaviorSanitizer (asan-ubsan preset)
# Compiler: the usual CMAKE_CXX_COMPILER / CXX environment variable.
#
# Targets: stage_01 .. stage_08 (one executable per tests/stage_NN.cpp that
# exists), ctest names stage-01 .. stage-08 with label stage-NN.
cmake_minimum_required(VERSION 3.23)

# CMake >= 4.0 no longer picks a macOS SDK by default; Homebrew GCC then cannot
# find <stdio.h>. Pick the active SDK before project() unless the user chose one.
if(CMAKE_HOST_APPLE AND NOT DEFINED CMAKE_OSX_SYSROOT AND NOT DEFINED ENV{SDKROOT})
  execute_process(COMMAND xcrun --show-sdk-path
                  OUTPUT_VARIABLE _cppatlas_sdk OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  if(_cppatlas_sdk)
    set(CMAKE_OSX_SYSROOT "${_cppatlas_sdk}" CACHE PATH "macOS SDK")
  endif()
endif()

project(cppatlas_vector LANGUAGES CXX)
enable_testing()

option(USE_STD "Alias my::vector to std::vector (tester self-check)" OFF)
option(CPPATLAS_SANITIZE "Enable AddressSanitizer + UndefinedBehaviorSanitizer" OFF)


set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# ccache, automatically, unless a launcher was chosen explicitly.
if(NOT CMAKE_CXX_COMPILER_LAUNCHER)
  find_program(CPPATLAS_CCACHE ccache)
  if(CPPATLAS_CCACHE)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CPPATLAS_CCACHE}")
  endif()
endif()

include(cmake/Sanitizers.cmake)

# --- the subject: which my/vector.hpp the tests see -------------------------
add_library(cppatlas_subject INTERFACE)
target_include_directories(cppatlas_subject INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
if(USE_STD)
  target_compile_definitions(cppatlas_subject INTERFACE CPPATLAS_USE_STD)
endif()

# --- warnings + sanitizers, shared by every TU -------------------------------
add_library(cppatlas_flags INTERFACE)
if(MSVC)
  target_compile_options(cppatlas_flags INTERFACE /W4 /permissive- /Zc:__cplusplus /EHsc /utf-8)
else()
  target_compile_options(cppatlas_flags INTERFACE -Wall -Wextra)
endif()
cppatlas_apply_sanitizers(cppatlas_flags)

# --- doctest main, compiled once ---------------------------------------------
add_library(cppatlas_doctest_main STATIC tests/support/doctest_main.cpp)
target_include_directories(cppatlas_doctest_main PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/tests/support")
target_link_libraries(cppatlas_doctest_main PUBLIC cppatlas_flags)

# --- harness self-test: always against std::vector, independent of the learner -
add_executable(harness_selftest tests/support/selftest.cpp)
target_compile_definitions(harness_selftest PRIVATE CPPATLAS_USE_STD)
target_link_libraries(harness_selftest PRIVATE cppatlas_doctest_main cppatlas_flags)
add_test(NAME harness-selftest COMMAND harness_selftest)
set_tests_properties(harness-selftest PROPERTIES LABELS "harness" TIMEOUT 60
  ENVIRONMENT "${CPPATLAS_TEST_ENV}")

# --- one executable + one ctest test per stage file that exists ---------------
set(CPPATLAS_STAGES "")
foreach(n RANGE 1 8)
  string(LENGTH "${n}" _len)
  if(_len EQUAL 1)
    set(nn "0${n}")
  else()
    set(nn "${n}")
  endif()
  set(src "${CMAKE_CURRENT_SOURCE_DIR}/tests/stage_${nn}.cpp")
  if(NOT EXISTS "${src}")
    continue()
  endif()
  add_executable(stage_${nn} "${src}")
  target_link_libraries(stage_${nn} PRIVATE cppatlas_subject cppatlas_doctest_main cppatlas_flags)
  add_test(NAME stage-${nn} COMMAND stage_${nn})
  set_tests_properties(stage-${nn} PROPERTIES
    LABELS "stage-${nn}"
    TIMEOUT 60
    ENVIRONMENT "${CPPATLAS_TEST_ENV}")
  list(APPEND CPPATLAS_STAGES "${nn}")
endforeach()

# Tells ./cppatlas which stages exist and how to run them (one value per line).
file(WRITE "${CMAKE_BINARY_DIR}/cppatlas-stages.txt" "")
foreach(nn IN LISTS CPPATLAS_STAGES)
  file(APPEND "${CMAKE_BINARY_DIR}/cppatlas-stages.txt" "${nn}\n")
endforeach()
string(REPLACE ";" "\n" _env_lines "${CPPATLAS_TEST_ENV}")
file(WRITE "${CMAKE_BINARY_DIR}/cppatlas-test-env.txt" "${_env_lines}\n")

message(STATUS "cppatlas: stages found: ${CPPATLAS_STAGES}")
if(USE_STD)
  set(_subject "std::vector (USE_STD=ON)")
else()
  set(_subject "include/my/vector.hpp")
endif()
message(STATUS "cppatlas: subject = ${_subject}; sanitizers = ${CPPATLAS_SANITIZE}")
