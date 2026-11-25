// tests/support/alloc_check.hpp - "every allocation made in a test is freed by its end".
//
// WHY. A vector that forgets to free a buffer (in ~vector(), after relocating,
// or on an exception path) leaks raw memory, not elements, so the element
// counters (tracked.hpp) cannot see it. LeakSanitizer can, but it does not run
// on macOS. This check works everywhere, sanitizers or not.
//
// HOW. doctest_main.cpp replaces the global operator new / operator delete
// (all of them: plain, array, nothrow, aligned, sized) with malloc-based
// versions that, WHILE A TEST BODY RUNS ON THE TEST THREAD, record each
// returned pointer in a fixed-size table (no allocation inside the hook), and
// erase it again when it is deleted. After the body returns, whatever is still
// in the table was allocated by the test and never freed:
//
//     stage_02.cpp:180: ERROR: 1 allocation(s) (64 bytes) made during this test were never freed ...
//
// FALSE-POSITIVE GUARDS (each was observed or is plausible):
//   - only allocations made AND still alive inside the body count; anything
//     allocated before the test (doctest, statics, the harness) is ignored,
//     and so is anything freed again before the body returns;
//   - doctest_main.cpp runs a warm-up before the first test that triggers the
//     usual one-time lazy allocations (iostream/locale numeric caches,
//     doctest's INFO/CAPTURE context stack);
//   - the check is reported only for a test that otherwise PASSED (a failed
//     check allocates its message, and a REQUIRE that throws skips it anyway);
//   - if the table overflows, the test is not checked (no report).
//
// LIMITS. Memory obtained with std::malloc / std::aligned_alloc directly is not
// seen (std::allocator<T> and ::operator new are). Only the thread that runs
// the test is tracked.
//
// ESCAPE HATCH. CPPATLAS_NO_ALLOC_CHECK=1 in the environment disables the
// check at run time; compiling doctest_main.cpp with -DCPPATLAS_NO_ALLOC_CHECK
// does not replace operator new at all. MSVC builds never replace it.
#pragma once

#include <cstddef>
#include <cstdint>

#if !defined(CPPATLAS_NO_ALLOC_CHECK) && !defined(_MSC_VER)
#define CPPATLAS_ALLOC_CHECK_COMPILED 1
#else
#define CPPATLAS_ALLOC_CHECK_COMPILED 0
#endif

namespace cppatlas::alloc_check {

namespace detail {

inline constexpr std::size_t kSlots = std::size_t{1} << 17;  // power of two
inline constexpr std::size_t kMaxRecorded = kSlots / 2;      // keep probing short
inline void* const kTombstone = reinterpret_cast<void*>(std::uintptr_t{1});

struct Slot {
    void* ptr;
    std::size_t bytes;
};

struct State {
    bool enabled = false;      // set by doctest_main.cpp: operators replaced AND not disabled by the env
    bool overflow = false;     // too many live allocations: the result is unreliable
    std::size_t used = 0;      // slots ever filled since clear() (live + tombstones)
    long live = 0;             // recorded allocations not freed yet
    std::size_t live_bytes = 0;
    long failures = 0;         // failed doctest asserts in the current test (listener in doctest_main.cpp)
    Slot table[kSlots] = {};
};

inline State& state() noexcept {
    static State s;  // constant-initialized storage: no allocation, no init order issue
    return s;
}

// The test thread, while a test body runs. Other threads are never recorded.
inline thread_local bool recording = false;

inline std::size_t hash(void* p) noexcept {
    const auto v = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(p));
    return static_cast<std::size_t>((v >> 4) * 0x9E3779B97F4A7C15ull) & (kSlots - 1);
}

inline void clear() noexcept {
    State& s = state();
    if (s.used != 0)
        for (Slot& slot : s.table) slot = Slot{nullptr, 0};
    s.used = 0;
    s.live = 0;
    s.live_bytes = 0;
    s.overflow = false;
}

// Called by the replaced operator new (doctest_main.cpp). Never allocates.
inline void on_alloc(void* p, std::size_t bytes) noexcept {
    if (!recording || p == nullptr) return;
    State& s = state();
    if (s.used >= kMaxRecorded) {
        s.overflow = true;
        return;
    }
    for (std::size_t i = hash(p);; i = (i + 1) & (kSlots - 1)) {
        Slot& slot = s.table[i];
        if (slot.ptr == nullptr || slot.ptr == kTombstone) {
            if (slot.ptr == nullptr) ++s.used;
            slot = Slot{p, bytes};
            ++s.live;
            s.live_bytes += bytes;
            return;
        }
    }
}

// Called by the replaced operator delete. Never allocates.
inline void on_free(void* p) noexcept {
    if (!recording || p == nullptr) return;
    State& s = state();
    if (s.live == 0) return;
    for (std::size_t i = hash(p);; i = (i + 1) & (kSlots - 1)) {
        Slot& slot = s.table[i];
        if (slot.ptr == nullptr) return;  // not recorded (allocated before the test)
        if (slot.ptr == p) {
            --s.live;
            s.live_bytes -= slot.bytes;
            slot = Slot{kTombstone, 0};
            return;
        }
    }
}

}  // namespace detail

// RAII: records allocations of this thread for the scope's lifetime.
// Scopes do not nest (the inner one would reset the outer one's table).
class Scope {
public:
    Scope() noexcept {
        detail::clear();
        detail::recording = detail::state().enabled;
    }
    ~Scope() { detail::recording = false; }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

    // Allocations made in this scope and not freed yet.
    long live() const noexcept { return detail::state().live; }
    std::size_t live_bytes() const noexcept { return detail::state().live_bytes; }
    // False when the check is off (not compiled, CPPATLAS_NO_ALLOC_CHECK) or the
    // table overflowed: then live() means nothing.
    bool reliable() const noexcept { return detail::state().enabled && !detail::state().overflow; }
    void stop() noexcept { detail::recording = false; }
};

// Is the check active in this process?
inline bool enabled() noexcept { return detail::state().enabled; }

// Failed doctest assertions in the running test case (maintained by a doctest
// listener in doctest_main.cpp; always 0 if that listener is not linked).
inline long failures_in_current_test() noexcept { return detail::state().failures; }

}  // namespace cppatlas::alloc_check
