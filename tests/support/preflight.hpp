// tests/support/preflight.hpp - one clear message per missing member, no error walls.
//
// THE PROBLEM. When the learner has not written `push_back` yet, every test
// that calls it produces a screenful of template errors, and the useful
// sentence ("you need push_back") drowns.
//
// THE MECHANISM (three pieces, all used by every stage TU):
//
//  1. CPPATLAS_PREFLIGHT(id, "message", Subject, requires-expression-on-V);
//     Defines `constexpr bool id` = whether Subject satisfies the requirement
//     and static_asserts it with "message". Missing member => exactly one
//     "static assertion failed: <message>" line per missing piece.
//     The requirement is written against the placeholder type `V` (bound to
//     Subject), which keeps it dependent so a missing member makes the check
//     false instead of being a hard error.
//
//  2. CPPATLAS_STAGE_GATE(id1 && id2 && ...);
//     ANDs all preflight results into `cppatlas_stage_ok`.
//
//  3. CPPATLAS_TEST("name") { ...body... }
//     Like doctest's TEST_CASE, but the body is a function template whose
//     template parameter is named `vector` and is bound to my::vector. Inside
//     the body you write `vector<int> v;`, which is a DEPENDENT type, so the
//     compiler does not look at `v.push_back(...)` until the template is
//     instantiated -- and it is only instantiated when cppatlas_stage_ok is
//     true. When preflight fails, the static_assert lines are the only errors.
//
// RULES for test authors (see README.md for the long version):
//   - Inside CPPATLAS_TEST bodies always write `vector<T>`, never `my::vector<T>`
//     (the latter is non-dependent and would be compiled eagerly).
//   - Helpers that touch a vector must be templates: `template <class V> void f(V& v)`.
//   - Every member a stage's tests use must be covered by a preflight check in
//     that stage (including members from earlier stages that it relies on).
//   - Subject must not contain a top-level comma (use an alias if needed).
//
// CPPATLAS_TEST also wraps every test body:
//   before: cppatlas::reset() (counters, errors, injection), trace marker
//   after (only if the body returned normally):
//           VERIFY(expect::lifetime_clean()); VERIFY(expect::all_destroyed(...))
//           and, if the test otherwise passed, VERIFY(expect::allocations_freed(...))
// so every test automatically fails with a diagnosis on leaked or
// double-destroyed elements, and on leaked storage (alloc_check.hpp).
#pragma once

#include <concepts>
#include <cstdio>
#include <cstdlib>

#include "alloc_check.hpp"
#include "diagnose.hpp"
#include "doctest.h"
#include "subject.hpp"
#include "tracked.hpp"
#include "trace.hpp"

#define CPPATLAS_CAT_IMPL_(a, b) a##b
#define CPPATLAS_CAT_(a, b) CPPATLAS_CAT_IMPL_(a, b)

#define CPPATLAS_PREFLIGHT(id, message, Subject, ...)                        \
    template <class V>                                                       \
    concept cppatlas_preflight_concept_##id = (__VA_ARGS__);                 \
    inline constexpr bool id = cppatlas_preflight_concept_##id<Subject>;    \
    static_assert(id, message)

#define CPPATLAS_STAGE_GATE(...) inline constexpr bool cppatlas_stage_ok = (__VA_ARGS__)

namespace cppatlas::detail {

// With CPPATLAS_PROGRESS=1 (set by ./cppatlas) every test announces itself on
// stderr, so a hanging or crashing test can be named. ./cppatlas strips these lines.
inline bool progress_enabled() noexcept {
    static const bool on = [] {
        const char* v = std::getenv("CPPATLAS_PROGRESS");
        return v != nullptr && *v != '\0' && *v != '0';
    }();
    return on;
}

// Runs one gated test body. `file`/`line` locate the CPPATLAS_TEST so the
// automatic end-of-test checks are reported at the test, not in this header.
template <bool Ok, class Body>
void run_stage_test(const char* name, const char* file, int line) {
    if constexpr (Ok) {
        ::cppatlas::reset();
        ::cppatlas::trace::test_begin(name);
        if (progress_enabled()) std::fprintf(stderr, "[cppatlas] running: %s\n", name);
        long leaked_allocs = 0;
        std::size_t leaked_bytes = 0;
        {
            ::cppatlas::alloc_check::Scope allocs;  // see alloc_check.hpp
            Body::template run<my::vector>();
            allocs.stop();
            if (allocs.reliable()) {
                leaked_allocs = allocs.live();
                leaked_bytes = allocs.live_bytes();
            }
        }
        // Automatic checks, skipped if the body already reported the same problem.
        const Ledger& l = ledger();
        if (!l.reported_lifetime) {
            const Verdict v = ::cppatlas::expect::lifetime_clean();
            if (!v.ok) DOCTEST_ADD_FAIL_CHECK_AT(file, line, v.message);
        }
        if (!l.reported_live) {
            const Verdict v = ::cppatlas::expect::all_destroyed("when the test ended (every vector in it is gone)");
            if (!v.ok) DOCTEST_ADD_FAIL_CHECK_AT(file, line, v.message);
        }
        // Only for a test that otherwise passed: a failure allocates its own
        // message, and one diagnosis at a time is enough.
        if (leaked_allocs > 0 && ::cppatlas::alloc_check::failures_in_current_test() == 0) {
            const Verdict v = ::cppatlas::expect::allocations_freed(leaked_allocs, leaked_bytes);
            if (!v.ok) DOCTEST_ADD_FAIL_CHECK_AT(file, line, v.message);
        }
    } else {
        (void)name;
        (void)file;
        (void)line;
    }
}

}  // namespace cppatlas::detail

#define CPPATLAS_TEST_IMPL_(name, body)                                                    \
    namespace {                                                                            \
    struct body {                                                                          \
        template <template <class...> class vector>                                        \
        static void run();                                                                 \
    };                                                                                     \
    }                                                                                      \
    TEST_CASE(name) {                                                                      \
        ::cppatlas::detail::run_stage_test<cppatlas_stage_ok, body>(name, __FILE__, __LINE__); \
    }                                                                                      \
    template <template <class...> class vector>                                            \
    void body::run()

#define CPPATLAS_TEST(name) CPPATLAS_TEST_IMPL_(name, CPPATLAS_CAT_(cppatlas_test_body_, __COUNTER__))
