// Stage 3 - growth
// Spec: https://cppatlas.com/build/vector/growth
//
// reserve(n) and geometric growth: N push_backs cost O(log N) reallocations
// and O(N) element relocations in total, whatever the growth factor (> 1).
// Relocation may copy OR move here (push_back(T&&) and move_if_noexcept come
// in stages 4 and 6), so relocations are counted as "copies + moves".
//
// Standard facts used ([vector.capacity] reserve): after reserve(n),
// capacity() >= n; reallocation happens iff capacity() < n, so reserve(n) with
// n <= capacity() changes nothing; no reallocation happens during insertions
// until size() would exceed capacity().
#include "support/harness.hpp"

#include <cmath>
#include <string>

using namespace cppatlas;

namespace preflight {

CPPATLAS_PREFLIGHT(stage1_api,
                   "stage 3 (growth) builds on stage 1: my::vector<T> needs `vector()`, "
                   "`size() const`, `capacity() const` and `empty() const` (run `./cppatlas test 1`).",
                   my::vector<Tracked>,
                   std::default_initializable<V> && requires(const V& v) {
                       { v.size() } -> std::convertible_to<std::size_t>;
                       { v.capacity() } -> std::convertible_to<std::size_t>;
                       { v.empty() } -> std::convertible_to<bool>;
                   });

CPPATLAS_PREFLIGHT(has_push_back,
                   "stage 3 (growth) builds on stage 2: my::vector<T> needs `void push_back(const T& value)`.",
                   my::vector<Tracked>,
                   requires(V& v, const Tracked& t) { v.push_back(t); });

CPPATLAS_PREFLIGHT(has_index,
                   "stage 3 (growth) builds on stage 2: my::vector<T> needs `T& operator[](std::size_t)` "
                   "and `const T& operator[](std::size_t) const`.",
                   my::vector<Tracked>,
                   requires(V& v, const V& cv, std::size_t i) {
                       { v[i] } -> std::same_as<Tracked&>;
                       { cv[i] } -> std::same_as<const Tracked&>;
                   });

CPPATLAS_PREFLIGHT(has_reserve,
                   "stage 3 (growth): my::vector<T> needs `void reserve(std::size_t new_cap)`.",
                   my::vector<Tracked>,
                   requires(V& v, std::size_t n) { v.reserve(n); });

}  // namespace preflight

CPPATLAS_STAGE_GATE(preflight::stage1_api && preflight::has_push_back && preflight::has_index &&
                    preflight::has_reserve);

namespace {

// Pushes Elem(first), Elem(first+1), ... (n of them, from const lvalues) and
// fails the test at the first push that does not add exactly one element.
template <class V>
void push_sequence(V& v, int first, int n) {
    using Elem = std::remove_cvref_t<decltype(v[0])>;
    const std::size_t start = v.size();
    for (int i = 0; i < n; ++i) {
        {
            const Elem e(first + i);
            v.push_back(e);
        }
        if (v.size() != start + static_cast<std::size_t>(i) + 1) {
            FAIL("after " << i + 1 << " push_back calls size() is " << v.size() << ", expected "
                          << start + static_cast<std::size_t>(i) + 1
                          << "\n-> push_back must add exactly one element.");
        }
    }
}

// Index of the first element that is not first+i, or -1.
template <class V>
long first_mismatch(const V& v, int first, int n) {
    for (int i = 0; i < n; ++i)
        if (!(v[static_cast<std::size_t>(i)] == first + i)) return i;
    return -1;
}

// Pushes 0, 1, 2, ... until size() == capacity() and size() >= min_size.
// Gives up (test failure) after `limit` pushes. Returns the final size.
template <class V>
std::size_t fill_to_capacity(V& v, std::size_t min_size, int limit = 4096) {
    using Elem = std::remove_cvref_t<decltype(v[0])>;
    for (int i = 0; i < limit; ++i) {
        if (v.size() >= min_size && v.size() == v.capacity()) return v.size();
        const Elem e(static_cast<int>(v.size()));
        v.push_back(e);
    }
    FAIL("after " << limit << " push_backs size() (" << v.size() << ") never reached capacity() (" << v.capacity()
                  << ")\n-> capacity() must report the number of allocated slots; size() reaches it right before "
                     "the next reallocation.");
    return 0;
}

}  // namespace

CPPATLAS_TEST("reserve(n) on an empty vector: capacity() >= n, still empty, no elements created") {
    vector<Tracked> v;
    CounterScope scope;
    v.reserve(10);
    VERIFY(expect::no_element_ops(scope.delta(), "reserve() on an empty vector",
                                  "reserve only allocates raw storage; it must not construct any T "
                                  "(no `new T[n]`)."));
    CHECK_MESSAGE(v.capacity() >= 10, "after reserve(10) capacity() is " << v.capacity() << ", expected >= 10");
    CHECK_MESSAGE(v.size() == 0, "reserve() must not change size(): it is " << v.size());
    CHECK(v.empty());
}

CPPATLAS_TEST("reserve(n) preserves size, values and order, and relocates each element once") {
    vector<Tracked> v;
    push_sequence(v, 100, 5);
    REQUIRE(v.size() == 5);
    const std::size_t want = v.capacity() + 50;
    CounterScope scope;
    v.reserve(want);
    const Counts d = scope.delta();
    CHECK_MESSAGE(v.capacity() >= want, "after reserve(" << want << ") capacity() is " << v.capacity());
    REQUIRE_MESSAGE(v.size() == 5, "reserve() must not change size(): it is " << v.size() << ", expected 5");
    const long bad = first_mismatch(v, 100, 5);
    CHECK_MESSAGE(bad == -1, "after reserve(), v[" << bad << "] is " << v[static_cast<std::size_t>(bad < 0 ? 0 : bad)]
                                                   << ", expected " << 100 + bad
                                                   << "\n-> relocate every element into the new buffer, in order.");
    // Copy or move are both fine at this stage; each element exactly once.
    VERIFY(expect::relocated(d, 5, "reserve()"));
    VERIFY(expect::live(5, "the vector holds 5 elements, so exactly 5 element objects must be alive"));
}

CPPATLAS_TEST("reserve(n) with n <= capacity() changes nothing (it never shrinks)") {
    vector<Tracked> v;
    v.reserve(40);
    push_sequence(v, 0, 6);
    REQUIRE(v.size() == 6);
    const std::size_t cap = v.capacity();
    REQUIRE(cap >= 40);
    const Tracked* first = &v[0];
    CounterScope scope;
    for (std::size_t n : {std::size_t{0}, std::size_t{1}, std::size_t{6}, std::size_t{10}, cap}) {
        v.reserve(n);
        INFO("after reserve(" << n << ") on a vector with size() == 6 and capacity() == " << cap);
        CHECK_MESSAGE(v.capacity() == cap, "capacity() became " << v.capacity()
                                                                << "\n-> reserve(n) must do nothing when n <= "
                                                                   "capacity(); in particular it never shrinks.");
        REQUIRE(v.size() == 6);
        CHECK_MESSAGE((&v[0] == first), "the elements were moved to a new buffer, but no reallocation was needed");
    }
    VERIFY(expect::no_element_ops(scope.delta(), "reserve(n) with n <= capacity()",
                                  "return early when n <= capacity(): no new buffer, no relocation."));
    CHECK(first_mismatch(v, 0, 6) == -1);
}

CPPATLAS_TEST("after reserve(n), push_back up to n elements never reallocates") {
    constexpr int n = 64;
    vector<Tracked> v;
    v.reserve(n);
    REQUIRE(v.capacity() >= static_cast<std::size_t>(n));
    const std::size_t cap = v.capacity();
    push_sequence(v, 0, 1);
    REQUIRE(v.size() == 1);
    const Tracked& first_ref = v[0];  // must stay valid until size() exceeds capacity()
    const Tracked* first = &v[0];
    CounterScope scope;
    bool stable = true;
    for (int i = 1; i < n && stable; ++i) {
        push_sequence(v, i, 1);
        if (v.capacity() != cap || &v[0] != first) {
            stable = false;
            FAIL_CHECK("push_back #" << i + 1 << " (size " << v.size() << " <= the reserved " << n
                                     << ") reallocated: capacity() " << cap << " -> " << v.capacity()
                                     << ", &v[0] " << static_cast<const void*>(first) << " -> "
                                     << static_cast<const void*>(&v[0])
                                     << "\n-> push_back may only reallocate when size() == capacity(); after "
                                        "reserve(n), references to elements stay valid until size() exceeds n.");
        }
    }
    REQUIRE(v.size() == static_cast<std::size_t>(n));
    const Counts d = scope.delta();
    // Each push_back copies its argument once; nothing else may happen.
    VERIFY(expect::ops(d, Op::copy_construct, n - 1,
                       "with spare capacity, push_back(const T&) copy-constructs exactly the new element; any other "
                       "copy means existing elements were relocated."));
    CHECK_MESSAGE(d.move_ctor == 0, "push_back with spare capacity moved elements: "
                                        << to_string(d)
                                        << "\n-> either existing elements were relocated although no reallocation was "
                                           "needed, or the new element was built as a temporary and then moved: "
                                           "construct it directly in the free slot from the argument.");
    if (stable)  // otherwise first_ref dangles (already reported above)
        CHECK_MESSAGE(first_ref == 0, "a reference to v[0] taken before the pushes no longer reads 0: " << first_ref);
    CHECK(first_mismatch(v, 0, n) == -1);
}

CPPATLAS_TEST("reserve on a non-empty vector, then pushes up to the reserved size keep addresses stable") {
    vector<NoDefault> v;  // also: reserve must not default-construct T
    push_sequence(v, 0, 3);
    REQUIRE(v.size() == 3);
    v.reserve(30);
    REQUIRE(v.capacity() >= 30);
    REQUIRE(v.size() == 3);
    const NoDefault* first = &v[0];
    const NoDefault* third = &v[2];
    bool stable = true;
    for (int i = 3; i < 30 && stable; ++i) {
        push_sequence(v, i, 1);
        stable = (&v[0] == first && &v[2] == third);
    }
    CHECK_MESSAGE(stable, "elements moved while size() stayed within the reserved capacity"
                          "\n-> only reallocate when the new size would exceed capacity().");
    REQUIRE(v.size() == 30);
    CHECK(first_mismatch(v, 0, 30) == -1);
}

CPPATLAS_TEST("push_back grows geometrically: O(log N) reallocations and O(N) relocations") {
    // N push_backs with growth factor f > 1 reallocate about log_f(N) times and
    // relocate about N / (f - 1) elements in total (2x: ~N, 1.5x: ~2N). The
    // bounds below accept any factor >= ~1.25; growing by a constant fails them.
    constexpr long N = 100000;
    const long max_reallocations = 3 * static_cast<long>(std::ceil(std::log2(static_cast<double>(N)))) + 10;
    constexpr long max_relocations = 4 * N;

    vector<Tracked> v;
    long reallocations = 0;
    std::size_t cap = v.capacity();
    CounterScope scope;
    for (long i = 0; i < N; ++i) {
        {
            const Tracked e(static_cast<int>(i % 1000));
            v.push_back(e);
        }
        if (v.size() != static_cast<std::size_t>(i) + 1) {
            FAIL("after " << i + 1 << " push_backs size() is " << v.size());
        }
        if (v.capacity() < v.size()) {
            FAIL("capacity() (" << v.capacity() << ") < size() (" << v.size() << ") after " << i + 1
                                << " push_backs\n-> capacity() must report the allocated number of slots.");
        }
        if (v.capacity() != cap) {
            ++reallocations;
            cap = v.capacity();
        }
        // Every push_back copies its argument once; anything beyond that is relocation.
        const Counts d = scope.delta();
        const long relocations = d.copy_ctor + d.move_ctor - (i + 1);
        if (reallocations > max_reallocations || relocations > max_relocations) {
            FAIL(reallocations << " reallocations and " << relocations << " element relocations for only " << i + 1
                               << " push_backs (capacity() is now " << cap << "); a geometric growth policy needs "
                               << "about log2(N) reallocations and at most a few times N relocations for N "
                                  "push_backs (allowed for N = "
                               << N << ": " << max_reallocations << " and " << max_relocations << ")."
                               << "\n-> grow geometrically: when size() == capacity(), allocate e.g. "
                                  "max(1, 2 * capacity()) slots, not capacity() + 1 (or + a constant). Otherwise N "
                                  "push_backs cost O(N^2).");
        }
    }
    REQUIRE(v.size() == static_cast<std::size_t>(N));
    INFO("geometric growth: " << reallocations << " reallocations for " << N << " push_backs");
    VERIFY(expect::live(N, "N elements in the vector must mean N live objects: every relocation must destroy "
                           "the old element after constructing the new one"));
    long bad = -1;
    for (long i = 0; i < N; ++i) {
        if (!(v[static_cast<std::size_t>(i)] == static_cast<int>(i % 1000))) {
            bad = i;
            break;
        }
    }
    CHECK_MESSAGE(bad == -1, "v[" << bad << "] has the wrong value after many reallocations");
}

CPPATLAS_TEST("each reallocation destroys every old element exactly once") {
    CounterScope scope;
    {
        vector<Tracked> v;
        for (int i = 0; i < 200; ++i) {
            push_sequence(v, i, 1);
            if (counts().live != i + 1) {
                FAIL_CHECK("after " << i + 1 << " push_backs " << counts().live
                                    << " element objects are alive, expected " << i + 1
                                    << "\n-> when relocating to a new buffer, destroy each old element exactly once "
                                       "(std::destroy_at) before freeing the old storage.");
                break;
            }
        }
        REQUIRE(v.size() == 200);
        CHECK(first_mismatch(v, 0, 200) == -1);
    }
    VERIFY(expect::all_destroyed());
    VERIFY(expect::lifetime_clean());
    VERIFY(expect::destroyed_exactly_once(scope.delta()));
}

CPPATLAS_TEST("v.push_back(v[0]) works when the vector is full (the argument aliases an element)") {
    vector<Tracked> v;
    const std::size_t n = fill_to_capacity(v, 3);
    REQUIRE(n >= 3);
    REQUIRE(v.size() == v.capacity());
    v.push_back(v[0]);  // must reallocate; v[0] lives in the OLD buffer
    REQUIRE(v.size() == n + 1);
    CHECK_MESSAGE(v[n] == 0, "the new last element is " << v[n] << ", expected a copy of v[0] (0)"
                                                        << "\n-> the argument may refer to an element of this very "
                                                           "vector. Construct the new element (or copy the old ones) "
                                                           "BEFORE destroying the old elements and freeing the old "
                                                           "buffer.");
    CHECK(first_mismatch(v, 0, static_cast<int>(n)) == -1);
    VERIFY(expect::lifetime_clean());

    const std::size_t m = fill_to_capacity(v, n + 2);
    REQUIRE(m >= n + 2);
    v.push_back(v[m - 1]);  // the last element, at full capacity again
    REQUIRE(v.size() == m + 1);
    CHECK(v[m] == v[m - 1].value());
}

CPPATLAS_TEST("v.push_back(v[0]) at full capacity with std::string elements") {
    vector<std::string> v;
    const std::string first(50, 'x');  // long enough to live on the heap
    v.push_back(first);
    for (int i = 0; i < 4096 && !(v.size() >= 3 && v.size() == v.capacity()); ++i) v.push_back(std::to_string(i));
    REQUIRE(v.size() == v.capacity());
    const std::size_t n = v.size();
    v.push_back(v[0]);
    REQUIRE(v.size() == n + 1);
    CHECK_MESSAGE(v[n] == first, "the new last element is \"" << v[n] << "\", expected a copy of v[0]"
                                                              << "\n-> copy the argument before the old buffer's "
                                                                 "elements are destroyed or freed.");
    CHECK(v[0] == first);
}
