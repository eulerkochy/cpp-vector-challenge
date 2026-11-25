# cmake/Sanitizers.cmake - ASan + UBSan for the asan-ubsan preset.
#
# Defines:
#   cppatlas_apply_sanitizers(<interface-target>)  adds compile+link flags
#   CPPATLAS_TEST_ENV                               list of VAR=value for ctest / ./cppatlas
#
# macOS + Homebrew LLVM caveat: the AddressSanitizer runtime shipped with some
# Homebrew LLVM releases (seen with 21.1.2 on macOS 26/27) deadlocks during
# start-up (spinning in __sanitizer::get_dyld_hdr) before main() runs. We probe
# the runtime at configure time; if it hangs, we fall back to linking against
# Apple's (Xcode / Command Line Tools) sanitizer runtime, which works, and tell
# the user. Linux and Apple clang are unaffected.

set(CPPATLAS_TEST_ENV "")

function(_cppatlas_probe_asan out_var)
  # usage: _cppatlas_probe_asan(<out> COMPILE <flags...> LINK <flags...>)
  cmake_parse_arguments(P "" "" "COMPILE;LINK" ${ARGN})
  set(dir "${CMAKE_BINARY_DIR}/CMakeFiles/cppatlas-asan-probe")
  file(MAKE_DIRECTORY "${dir}")
  file(WRITE "${dir}/probe.cpp"
    "#include <string>\n#include <vector>\nint main() { std::vector<std::string> v(3, std::string(64, 'x')); return v.size() == 3 ? 0 : 1; }\n")
  set(sysroot "")
  if(APPLE AND CMAKE_OSX_SYSROOT)
    set(sysroot -isysroot "${CMAKE_OSX_SYSROOT}")
  endif()
  execute_process(
    COMMAND "${CMAKE_CXX_COMPILER}" -std=c++23 ${sysroot} ${P_COMPILE} -c "${dir}/probe.cpp" -o "${dir}/probe.o"
    RESULT_VARIABLE rc OUTPUT_QUIET ERROR_VARIABLE err)
  if(NOT rc EQUAL 0)
    set(${out_var} "compile-failed: ${err}" PARENT_SCOPE)
    return()
  endif()
  execute_process(
    COMMAND "${CMAKE_CXX_COMPILER}" ${sysroot} ${P_COMPILE} ${P_LINK} "${dir}/probe.o" -o "${dir}/probe"
    RESULT_VARIABLE rc OUTPUT_QUIET ERROR_VARIABLE err)
  if(NOT rc EQUAL 0)
    set(${out_var} "link-failed: ${err}" PARENT_SCOPE)
    return()
  endif()
  execute_process(COMMAND "${dir}/probe" TIMEOUT 20 RESULT_VARIABLE rc OUTPUT_QUIET ERROR_VARIABLE err)
  if(rc EQUAL 0)
    set(${out_var} "ok" PARENT_SCOPE)
  else()
    set(${out_var} "run-failed (${rc}): ${err}" PARENT_SCOPE)
  endif()
endfunction()

set(_cppatlas_san_compile "")
set(_cppatlas_san_link "")

if(CPPATLAS_SANITIZE)
  if(MSVC)
    # Best effort: MSVC has ASan but no UBSan.
    set(_cppatlas_san_compile /fsanitize=address /Zi)
    set(_cppatlas_san_link "")
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set(_cppatlas_san_compile
      -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all -g)
    set(_cppatlas_san_link -fsanitize=address,undefined)

    if(APPLE AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
      # Cache the probe per compiler so re-configures stay fast.
      if(NOT CPPATLAS_ASAN_PROBED_FOR STREQUAL "${CMAKE_CXX_COMPILER}")
        _cppatlas_probe_asan(_res COMPILE ${_cppatlas_san_compile} LINK ${_cppatlas_san_link})
        set(_mode "native")
        if(NOT _res STREQUAL "ok")
          message(STATUS "cppatlas: this compiler's own ASan runtime does not work here (${_res})")
          execute_process(COMMAND xcrun --find clang OUTPUT_VARIABLE _apple_clang
                          OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
          set(_apple_rd "")
          if(_apple_clang)
            execute_process(COMMAND "${_apple_clang}" -print-resource-dir OUTPUT_VARIABLE _apple_rd
                            OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
          endif()
          if(_apple_rd AND EXISTS "${_apple_rd}/lib/darwin/libclang_rt.asan_osx_dynamic.dylib")
            _cppatlas_probe_asan(_res2
              COMPILE ${_cppatlas_san_compile} -mllvm -asan-guard-against-version-mismatch=0
              LINK ${_cppatlas_san_link} "-resource-dir=${_apple_rd}")
            if(_res2 STREQUAL "ok")
              set(_mode "apple-runtime:${_apple_rd}")
            else()
              message(FATAL_ERROR
                "cppatlas: AddressSanitizer does not run with ${CMAKE_CXX_COMPILER} "
                "(${_res}), and the Apple-runtime fallback failed too (${_res2}). "
                "Use the debug preset, Apple clang (CXX=/usr/bin/clang++), or a newer LLVM.")
            endif()
          else()
            message(FATAL_ERROR
              "cppatlas: AddressSanitizer does not run with ${CMAKE_CXX_COMPILER} (${_res}) and "
              "no Apple sanitizer runtime was found. Use the debug preset or CXX=/usr/bin/clang++.")
          endif()
        endif()
        set(CPPATLAS_ASAN_MODE "${_mode}" CACHE INTERNAL "")
        set(CPPATLAS_ASAN_PROBED_FOR "${CMAKE_CXX_COMPILER}" CACHE INTERNAL "")
      endif()
      if(CPPATLAS_ASAN_MODE MATCHES "^apple-runtime:(.*)$")
        set(_apple_rd "${CMAKE_MATCH_1}")
        message(STATUS "cppatlas: WORKAROUND - linking Apple's sanitizer runtime from ${_apple_rd} "
                       "(this LLVM's ASan runtime hangs at start-up on this macOS)")
        # SHELL: keeps the "-mllvm <opt>" pair together (target options are de-duplicated).
        list(APPEND _cppatlas_san_compile "SHELL:-mllvm -asan-guard-against-version-mismatch=0")
        list(APPEND _cppatlas_san_link "-resource-dir=${_apple_rd}")
      endif()
    endif()
  else()
    message(WARNING "cppatlas: don't know how to enable sanitizers for ${CMAKE_CXX_COMPILER_ID}")
  endif()

  # Leak detection: on by default with ASan on Linux. LeakSanitizer is not
  # supported on macOS (detect_leaks=1 makes the Apple runtime abort), so we
  # never force it; element leaks are caught by the harness's live counter.
  set(_asan_opts "halt_on_error=1")
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    string(APPEND _asan_opts ":detect_leaks=1")
  endif()
  set(CPPATLAS_TEST_ENV
    "ASAN_OPTIONS=${_asan_opts}"
    "UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1")
endif()

function(cppatlas_apply_sanitizers target)
  if(_cppatlas_san_compile)
    target_compile_options(${target} INTERFACE ${_cppatlas_san_compile})
  endif()
  if(_cppatlas_san_link)
    target_link_options(${target} INTERFACE ${_cppatlas_san_link})
  endif()
endfunction()
