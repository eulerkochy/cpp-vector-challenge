# Writing stage tests

One file per stage: `tests/stage_NN.cpp` -> executable `stage_NN`, ctest test
`stage-NN`, label `stage-NN`. CMake picks up any `tests/stage_01.cpp` ..
`tests/stage_08.cpp` that exists; `./cppatlas` re-configures automatically when
a new one appears. Use `stage_01.cpp` / `stage_02.cpp` as the templates.

## The skeleton (copy it)

```cpp
// Stage 3 - growth
// Spec: https://cppatlas.com/build/vector/growth
#include "support/harness.hpp"

using namespace cppatlas;

namespace preflight {
// One check per member the tests in THIS file use - including members from
// earlier stages (push_back, operator[], size...). Requirement is written
// against the placeholder `V`, which is bound to the Subject type.
CPPATLAS_PREFLIGHT(has_reserve,
    "stage 3 (growth): my::vector<T> needs `void reserve(std::size_t new_cap)`.",
    my::vector<Tracked>,
    requires(V& v, std::size_t n) { v.reserve(n); });
CPPATLAS_PREFLIGHT(has_push_back, "...", my::vector<Tracked>,
    requires(V& v, const Tracked& t) { v.push_back(t); });
}  // namespace preflight

CPPATLAS_STAGE_GATE(preflight::has_reserve && preflight::has_push_back);

CPPATLAS_TEST("reserve relocates elements by move") {
    vector<Tracked> v;          // NOT my::vector<Tracked> - see rule 1
    v.reserve(2);
    v.push_back(Tracked(1));
    v.push_back(Tracked(2));
    REQUIRE(v.size() == 2);     // REQUIRE before indexing - see rule 3
    CounterScope scope;
    v.reserve(100);
    VERIFY(expect::relocated_by_move(scope.delta(), 2));
}
```

## How the gating works (why rule 1 matters)

`CPPATLAS_PREFLIGHT` defines a `constexpr bool` and `static_assert`s it with the
message, so a missing member produces exactly one line per missing piece:

```
clang: stage_02.cpp:30:20: error: static assertion failed due to requirement 'has_push_back': stage 2 (push-back): my::vector<T> needs `void push_back(const T& value)`.
gcc:   stage_02.cpp:30:20: error: static assertion failed: stage 2 (push-back): my::vector<T> needs `void push_back(const T& value)`.
```

`CPPATLAS_TEST(name) { body }` expands to a doctest `TEST_CASE` that calls
`body` as a function template `template <template <class...> class vector>`.
Inside the body, `vector<int>` is a *dependent* type, so the compiler does not
check `v.push_back(...)` until the template is instantiated - and
`run_stage_test<cppatlas_stage_ok, Body>` only instantiates it
(`if constexpr`) when every preflight check passed. Measured on stage 2 with
`push_back` deleted: 1 error with the gate vs. 20 errors / ~200 lines without
it (both GCC 16 and Clang 21). `-fmax-errors=1` / `-ferror-limit=1` was
rejected: it would also hide the second missing member and any real error in
the learner's header, and is compiler-specific.

`./cppatlas` recognises the `static assertion failed` lines and prints them as
a bullet list ("Your vector is missing something this stage needs").

## Rules

1. Inside `CPPATLAS_TEST` bodies always spell the type `vector<T>`, never
   `my::vector<T>` (non-dependent, compiled eagerly -> error wall). Helpers that
   take a vector must be templates: `template <class V> void fill(V& v, int n)`.
   Non-template helpers must not touch vector members.
2. Preflight every member the file uses, with a message that names the stage,
   its slug and the exact signature to add. For "must NOT compile" properties
   use `static_assert(!requires { ... })` inside a test or preflight; never match
   on compiler diagnostic text.
3. A wrong implementation must fail, not crash/hang/UB: `REQUIRE(v.size() == n)`
   before indexing; loop over the *expected* count, never over `v.size()`;
   prefer one summarising check (`first_mismatch`) over a CHECK per element.
4. Only assert what the standard guarantees - every test must pass with
   `-DUSE_STD=ON` on libstdc++ AND libc++. No exact growth factor (MSVC 1.5x,
   others 2x: assert amortized/geometric behaviour, e.g. number of
   reallocations over N push_backs is O(log N)), no `capacity() == 0` after
   default construction, no exact moved-from state of std types, no exact
   count of relocations triggered by a push_back (test relocation through
   `reserve()` instead).
5. Every failure explains itself: `INFO("...")` for context, `CHECK_MESSAGE`,
   or `VERIFY(expect::...)`. End hints with `-> likely cause / fix`.
   Add reusable diagnoses to `diagnose.hpp`, not inline.
6. Compare tracked elements with ints (`v[0] == 3`), not temporaries
   (`v[0] == Tracked(3)` adds a construction to the counters).

## Support API

Headers (all included by `harness.hpp`):

| header | contents |
|---|---|
| `subject.hpp` | `my::vector` = `<my/vector.hpp>`, or `std::vector` alias under `CPPATLAS_USE_STD`; `cppatlas::testing_std_vector` |
| `tracked.hpp` | instrumented types, `Counts`, `counts()`, `CounterScope`, `reset()`, `clear_lifetime_errors()`, `ThrowOn`, `InjectedError`, `Op` |
| `diagnose.hpp` | `Verdict`, `Message`, `cppatlas::expect::*`, `VERIFY` / `REQUIRE_VERIFY` |
| `preflight.hpp` | `CPPATLAS_PREFLIGHT`, `CPPATLAS_STAGE_GATE`, `CPPATLAS_TEST` |
| `trace.hpp` | optional JSON Lines trace (`CPPATLAS_TRACE=path`) |
| `alloc_check.hpp` | leaked-storage check: `alloc_check::Scope`, `enabled()`, `failures_in_current_test()` (the replaced `operator new`/`delete` live in `doctest_main.cpp`) |

Element types (all in `cppatlas`): `Tracked` (= `NoexceptMove`),
`MayThrowMove`, `CopyOnly`, `MoveOnly`, `MoveOnlyMayThrow` (move-only with a
throwing move: relocated by move, basic guarantee only), `NoDefault`,
`MultiArg` + `ArgProbe` / `ArgKind`. Each has `value()`, `id()`, `moved_from()`, `== int`, `operator<<`
(`Tracked#7{42}`, `Tracked#7{moved-from}`). Constructors from `int` are
`explicit`.

Counters (`Counts` fields): `default_ctor value_ctor copy_ctor move_ctor
copy_assign move_assign dtor live bad_destroy use_after_destroy assign_to_raw
injected`; helpers `constructions() copies() moves() assignments()
element_ops() lifetime_errors() get(Op)`; `to_string(Counts)`.

```cpp
CounterScope scope;               // snapshot
v.reserve(10);
Counts d = scope.delta();         // events since the snapshot; d.live = net change
scope.restart();                  // new snapshot
```

Counters are global (all element types share one ledger), reset automatically
at the start of each `CPPATLAS_TEST`. After each test body returns, the wrapper
automatically checks `expect::lifetime_clean()` and `expect::all_destroyed()`
(skipped if the body already reported that problem; a test that reports a
live-count problem itself should set `detail::ledger().reported_live = true`),
and - only if the test otherwise passed - that every allocation made during the
body was freed (`expect::allocations_freed`, see "Allocation check" below).

Exception injection:

```cpp
{
    ThrowOn arm(Op::copy_construct, 2);   // 2nd copy constructor call from now throws, once
    CHECK_THROWS_AS(v.reserve(64), InjectedError);
    CHECK(arm.fired());
}                                        // disarmed
```

Injectable: `default_construct value_construct copy_construct move_construct
copy_assign move_assign`. Move injection is ignored by types with a noexcept
move (it would `std::terminate`) - use `MayThrowMove`. A constructor that throws
is not counted as a construction.

Verdicts (`cppatlas::expect`, use with `VERIFY(...)`):

| factory | passes when |
|---|---|
| `ops(d, Op, n, hint)` | exactly n of that op in window d |
| `no_element_ops(d, what, hint)` | no ctor/assign/dtor in d (e.g. vector move is O(1)) |
| `no_default_constructions(d)` | `d.default_ctor == 0` |
| `all_destroyed(when)` | `counts().live == 0` now |
| `live(n, hint)` | `counts().live == n` now |
| `destroyed_exactly_once(d)` | every object created in d was destroyed once |
| `lifetime_clean()` | no bad_destroy / use_after_destroy / assign_to_raw |
| `relocated_by_move(d, n)` | n move ctors, 0 copies (d must contain only the relocation) |
| `relocated_by_copy(d, n)` | n copy ctors, 0 moves (strong guarantee for may-throw moves) |
| `relocated(d, n, what)` | n copy-or-move ctors + n dtors, nothing else (stage 3: kind not yet fixed) |
| `no_copies(d, what, hint)` | no copy ctor / copy assignment in d |
| `at_most(got, bound, what, hint)` | got <= bound (for unspecified-but-bounded counts) |
| `unchanged(snapshot, v, what, hint)` | v has the same size, capacity, &v[0], element ids and values |
| `allocations_freed(n, bytes)` | n == 0 (used by the automatic allocation check) |
| `first_failure({v1, v2, ...})` | all pass; otherwise returns the FIRST failing verdict (one message for a group of checks that fail for the same reason) |

`Snapshot s = snapshot(v);` records size/capacity/&v[0]/ids/values of a vector
of instrumented elements (only `size()`, `capacity()`, `operator[] const`);
`expect::unchanged(s, v, ...)` is the "no effects" check for the strong
exception guarantee (stage 6) and address stability.

doctest gotchas: a non-literal `const char*` streamed into `FAIL_CHECK` /
`CAPTURE` prints as a bool (`1`) - use `std::string`; `CAPTURE` keeps a
reference, so never `CAPTURE(f())` of a temporary; a decomposed pointer
comparison prints as `1 == 1`, so write `CHECK_MESSAGE((p == q), "...")`.

Write your own: `Verdict f(...) { if (ok) return Verdict::pass(); return Verdict::fail(Message() << ... << "\n-> hint"); }`.

## Allocation check

Element counters cannot see a leaked *buffer* (all elements destroyed, storage
never freed), and LeakSanitizer does not run on macOS. So `doctest_main.cpp`
replaces every global `operator new` / `operator delete` (plain, array,
nothrow, aligned, sized) with malloc-based versions that record, in a fixed
table and only on the test thread while a `CPPATLAS_TEST` body runs, every
pointer allocated and not yet freed. Whatever is left when the body returns is
reported - only if the test otherwise passed - as
`N allocation(s) (B bytes) made during this test were never freed ...`.

- The operators are in the TU that defines `main()`, so they are always linked
  into every executable (an object in a static library is only pulled in when
  referenced; `main` guarantees that).
- Allocations made before the body (doctest, statics) and allocations freed
  before the body returns never count. One-time lazy allocations (locale
  numeric caches, doctest's INFO/CAPTURE stack) are triggered by a warm-up in
  `main()` before the first test.
- Not seen: memory from `std::malloc` / `aligned_alloc` called directly.
- Off switch: `CPPATLAS_NO_ALLOC_CHECK=1` at run time, or compile
  `doctest_main.cpp` with `-DCPPATLAS_NO_ALLOC_CHECK` (no replacement at all).
  MSVC never replaces them.
- Inside a test body do not open your own `alloc_check::Scope` (scopes do not
  nest); `selftest.cpp` shows how to test the mechanism from a plain
  `TEST_CASE`.

## Trace

`CPPATLAS_TRACE=trace.jsonl ./build/debug/stage_02` (or `=-` for stderr)
writes one JSON object per element event; see the header of `trace.hpp` for the
schema. `trace::note("text")` adds markers. Compile out with
`-DCPPATLAS_NO_TRACE`.

