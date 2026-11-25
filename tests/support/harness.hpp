// tests/support/harness.hpp - the only header a stage TU includes.
//
//   #include "support/harness.hpp"
//
// Pulls in: doctest (declarations only; main() lives in doctest_main.cpp, built
// once), subject.hpp (my::vector), tracked.hpp (instrumented element types,
// counters, exception injection), diagnose.hpp (VERIFY + cppatlas::expect),
// preflight.hpp (CPPATLAS_PREFLIGHT / CPPATLAS_STAGE_GATE / CPPATLAS_TEST),
// trace.hpp (optional JSON trace), alloc_check.hpp (leaked-storage check).
// See README.md in this directory.
#pragma once

#include <concepts>
#include <cstddef>
#include <iterator>
#include <string>
#include <type_traits>
#include <utility>

#include "alloc_check.hpp"
#include "doctest.h"
#include "subject.hpp"
#include "trace.hpp"
#include "tracked.hpp"
#include "diagnose.hpp"
#include "preflight.hpp"
