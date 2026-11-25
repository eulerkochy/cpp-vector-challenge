// Stage 5 - emplace-back
// Spec: https://cppatlas.com/build/vector/emplace-back
//
// emplace_back(Args&&...) -> T&, pop_back(), clear() noexcept.
//
// Standard facts used:
//   - emplace_back constructs the element in place with
//     allocator_traits::construct(a, p, std::forward<Args>(args)...), i.e.
//     `::new (p) T(std::forward<Args>(args)...)` - PARENTHESES, so for a type
//     with an initializer_list constructor emplace_back(3, 7) is T(3, 7), not
//     T{3, 7}. With spare capacity that is exactly one constructor call.
//   - since C++17 it returns a reference to the inserted element (back()).
//   - pop_back destroys the last element and invalidates only iterators and
//     references to it (so nothing is reallocated: capacity is unchanged).
//   - clear() is noexcept and destroys every element. "clear() leaves
//     capacity() unchanged" is not spelled out in [vector], but it is what every
//     implementation does (and what the spec asks for); it follows from
//     reserve()'s guarantee that later insertions do not reallocate.
#include "support/harness.hpp"

#include <memory>
#include <vector>  // std::vector<int> is used as an ELEMENT type below

using namespace cppatlas;

namespace preflight {

CPPATLAS_PREFLIGHT(stage1_api,
                   "stage 5 (emplace-back) builds on stage 1: my::vector<T> needs `vector()`, "
                   "`size() const`, `capacity() const` and `empty() const` (run `./cppatlas test 1`).",
                   my::vector<Tracked>,
                   std::default_initializable<V> && requires(const V& v) {
                       { v.size() } -> std::convertible_to<std::size_t>;
                       { v.capacity() } -> std::convertible_to<std::size_t>;
                       { v.empty() } -> std::convertible_to<bool>;
                   });

CPPATLAS_PREFLIGHT(stage2_api,
                   "stage 5 (emplace-back) builds on stage 2: my::vector<T> needs `void push_back(const T&)`, "
                   "`T& operator[](std::size_t)` and `const T& operator[](std::size_t) const`.",
                   my::vector<Tracked>,
                   requires(V& v, const V& cv, const Tracked& t, std::size_t i) {
                       v.push_back(t);
                       { v[i] } -> std::same_as<Tracked&>;
                       { cv[i] } -> std::same_as<const Tracked&>;
                   });

CPPATLAS_PREFLIGHT(has_reserve,
                   "stage 5 (emplace-back) builds on stage 3: my::vector<T> needs `void reserve(std::size_t)`.",
                   my::vector<Tracked>,
                   requires(V& v, std::size_t n) { v.reserve(n); });

CPPATLAS_PREFLIGHT(has_emplace_back,
                   "stage 5 (emplace-back): my::vector<T> needs "
                   "`template <class... Args> T& emplace_back(Args&&... args)`.",
                   my::vector<Tracked>,
                   requires(V& v) { v.emplace_back(1); });

CPPATLAS_PREFLIGHT(emplace_back_is_variadic,
                   "stage 5 (emplace-back): emplace_back must accept ANY constructor arguments - make it a variadic "
                   "template `template <class... Args> T& emplace_back(Args&&... args)` that forwards them with "
                   "std::forward<Args>(args)...",
                   my::vector<MultiArg>,
                   // (only reported once emplace_back exists at all)
                   !requires(V& v) { v.emplace_back(1); } ||
                   (requires(V& v, ArgProbe& p, std::unique_ptr<int> u) {
                       v.emplace_back(1, 2);
                       v.emplace_back(1, p);
                       v.emplace_back(std::move(u));
                   }));

CPPATLAS_PREFLIGHT(emplace_back_no_args,
                   "stage 5 (emplace-back): emplace_back() must also work with NO arguments (value-initializes "
                   "the element): make it a variadic template `template <class... Args> T& "
                   "emplace_back(Args&&... args)`.",
                   my::vector<Tracked>,
                   !requires(V& v) { v.emplace_back(1); } || requires(V& v) { v.emplace_back(); });

CPPATLAS_PREFLIGHT(emplace_back_returns_reference,
                   "stage 5 (emplace-back): emplace_back must return a reference to the new element: "
                   "`T& emplace_back(Args&&... args)` (C++17; `void` is the C++11 signature).",
                   my::vector<Tracked>,
                   !requires(V& v) { v.emplace_back(1); } || requires(V& v) {
                       { v.emplace_back(1) } -> std::same_as<Tracked&>;
                   });

CPPATLAS_PREFLIGHT(has_pop_back,
                   "stage 5 (emplace-back): my::vector<T> needs `void pop_back()`.",
                   my::vector<Tracked>,
                   requires(V& v) { v.pop_back(); });

CPPATLAS_PREFLIGHT(has_clear,
                   "stage 5 (emplace-back): my::vector<T> needs `void clear() noexcept`.",
                   my::vector<Tracked>,
                   requires(V& v) { v.clear(); });

}  // namespace preflight

CPPATLAS_STAGE_GATE(preflight::stage1_api && preflight::stage2_api && preflight::has_reserve &&
                    preflight::has_emplace_back && preflight::emplace_back_is_variadic && preflight::emplace_back_no_args &&
                    preflight::emplace_back_returns_reference && preflight::has_pop_back && preflight::has_clear);

namespace {

// emplace_back(first + i) for i in [0, n); fails at the first call that does
// not add exactly one element.
template <class V>
void emplace_values(V& v, int first, int n) {
    const std::size_t start = v.size();
    for (int i = 0; i < n; ++i) {
        v.emplace_back(first + i);
        if (v.size() != start + static_cast<std::size_t>(i) + 1)
            FAIL("after " << i + 1 << " emplace_back calls size() is " << v.size() << ", expected "
                          << start + static_cast<std::size_t>(i) + 1);
    }
}

template <class V>
long first_mismatch(const V& v, int first, int n) {
    for (int i = 0; i < n; ++i)
        if (!(v[static_cast<std::size_t>(i)] == first + i)) return i;
    return -1;
}

}  // namespace

// ------------------------------------------------------------- emplace_back

CPPATLAS_TEST("emplace_back with spare capacity: exactly one constructor call, no copy, no move") {
    vector<Tracked> v;
    v.reserve(4);
    CounterScope scope;
    v.emplace_back(7);
    const Counts d = scope.delta();
    REQUIRE(v.size() == 1);
    CHECK(v[0] == 7);
    VERIFY(expect::ops(d, Op::value_construct, 1,
                       "emplace_back(7) must construct the element directly in the free slot: "
                       "std::construct_at(data_ + size_, std::forward<Args>(args)...). Building a T first and then "
                       "copying/moving it into place defeats the purpose of emplace_back."));
    CHECK_MESSAGE(d.element_ops() == 1, "emplace_back(7) did more than one constructor call: " << to_string(d));
}

CPPATLAS_TEST("emplace_back() with no arguments value-initializes the element") {
    vector<Tracked> v;
    v.reserve(2);
    CounterScope scope;
    v.emplace_back();
    const Counts d = scope.delta();
    REQUIRE(v.size() == 1);
    CHECK(v[0] == 0);
    VERIFY(expect::ops(d, Op::default_construct, 1));
    CHECK(d.element_ops() == 1);
}

CPPATLAS_TEST("emplace_back forwards value categories (lvalue, const lvalue, rvalue)") {
    vector<MultiArg> v;
    v.reserve(4);
    ArgProbe probe{5};
    const ArgProbe cprobe{6};
    v.emplace_back(1, probe);
    v.emplace_back(2, cprobe);
    v.emplace_back(3, std::move(probe));
    REQUIRE(v.size() == 3);
    CHECK_MESSAGE(v[0].arg_kind() == ArgKind::lvalue,
                  "emplace_back(1, probe) with a non-const lvalue reached the constructor as "
                      << v[0].arg_kind() << "\n-> forward each argument with std::forward<Args>(args)...: an lvalue "
                                            "must stay an lvalue (and must not be moved from).");
    CHECK_MESSAGE(v[1].arg_kind() == ArgKind::const_lvalue,
                  "emplace_back(2, cprobe) with a const lvalue reached the constructor as " << v[1].arg_kind());
    CHECK_MESSAGE(v[2].arg_kind() == ArgKind::rvalue,
                  "emplace_back(3, std::move(probe)) reached the constructor as "
                      << v[2].arg_kind()
                      << "\n-> take the arguments as forwarding references (Args&&... args) and pass "
                         "std::forward<Args>(args)...; a named parameter is an lvalue, so without std::forward the "
                         "rvalue becomes an lvalue.");
    CHECK(v[0] == 1);
    CHECK(v[0].second() == 5);
    CHECK(v[1].second() == 6);
    CHECK(v[2].second() == 5);
    CHECK_MESSAGE(probe.moved_from, "the rvalue argument was not moved from");
}

CPPATLAS_TEST("emplace_back with several arguments and explicit constructors") {
    vector<MultiArg> v;
    v.reserve(2);
    CounterScope scope;
    v.emplace_back(4, 9);  // MultiArg(int, int) is explicit: only direct-initialization works
    const Counts d = scope.delta();
    REQUIRE(v.size() == 1);
    CHECK(v[0] == 4);
    CHECK_MESSAGE(v[0].second() == 9, "the second constructor argument was lost");
    CHECK(d.value_ctor == 1);
    CHECK(d.element_ops() == 1);
}

CPPATLAS_TEST("emplace_back with a move-only argument (std::unique_ptr)") {
    vector<MultiArg> v;
    auto p = std::make_unique<int>(42);
    v.emplace_back(std::move(p));
    REQUIRE(v.size() == 1);
    CHECK(v[0] == 42);
    CHECK_MESSAGE(p == nullptr, "the unique_ptr argument must have been moved into the constructor");
    v.emplace_back(std::make_unique<int>(43));
    REQUIRE(v.size() == 2);
    CHECK(v[1] == 43);
}

CPPATLAS_TEST("emplace_back returns a reference to the new last element") {
    vector<Tracked> v;
    v.reserve(4);
    emplace_values(v, 0, 2);
    REQUIRE(v.size() == 2);
    CHECK((std::is_same_v<decltype(v.emplace_back(1)), Tracked&>));
    Tracked& r = v.emplace_back(5);
    REQUIRE(v.size() == 3);
    CHECK_MESSAGE((&r == &v[2]), "emplace_back must return a reference to the element it just constructed (the last "
                               "one), i.e. data_[size_ - 1] after incrementing size_");
    r = Tracked(6);
    CHECK(v[2] == 6);

    // Also when emplace_back reallocates: the reference must point into the NEW buffer.
    vector<Tracked> w;
    for (int i = 0; i < 100; ++i) {
        Tracked& ri = w.emplace_back(i);
        if (w.size() != static_cast<std::size_t>(i) + 1 || &ri != &w[static_cast<std::size_t>(i)]) {
            FAIL_CHECK("emplace_back #" << i + 1 << " returned a reference that is not &v[size() - 1]"
                                        << "\n-> after a reallocation, return the element in the new buffer.");
            break;
        }
    }
}

CPPATLAS_TEST("emplace_back keeps the old elements when it reallocates") {
    vector<Tracked> v;
    emplace_values(v, 0, 100);
    REQUIRE(v.size() == 100);
    const long bad = first_mismatch(v, 0, 100);
    CHECK_MESSAGE(bad == -1, "v[" << bad << "] has the wrong value after reallocations in emplace_back");
    VERIFY(expect::live(100, "100 elements must be 100 live objects: destroy the old buffer's elements after "
                             "relocating them"));

    vector<MoveOnly> m;  // the only way to relocate a move-only type is to move it
    emplace_values(m, 0, 40);
    REQUIRE(m.size() == 40);
    CHECK(first_mismatch(m, 0, 40) == -1);
}

CPPATLAS_TEST("v.emplace_back(v[0]) works when the vector is full (the argument aliases an element)") {
    vector<Tracked> v;
    for (int i = 0; i < 4096 && !(v.size() >= 3 && v.size() == v.capacity()); ++i)
        v.emplace_back(static_cast<int>(v.size()));
    REQUIRE(v.size() >= 3);
    REQUIRE(v.size() == v.capacity());
    const std::size_t n = v.size();
    v.emplace_back(v[0]);
    REQUIRE(v.size() == n + 1);
    CHECK_MESSAGE(v[n] == 0, "the new last element is " << v[n] << ", expected a copy of v[0] (0)"
                                                        << "\n-> the arguments may refer to an element of this vector: "
                                                           "construct the new element in the NEW buffer before "
                                                           "relocating/destroying the old elements.");
    CHECK(first_mismatch(v, 0, static_cast<int>(n)) == -1);
}

CPPATLAS_TEST("emplace_back uses parentheses, not braces (initializer_list constructors)") {
    // std::vector<int>(3, 7) is {7, 7, 7}; std::vector<int>{3, 7} is {3, 7}.
    // Both paths: into an empty vector (reallocates) and with spare capacity.
    for (int spare = 0; spare < 2; ++spare) {
        CAPTURE(spare);
        vector<std::vector<int>> v;
        if (spare) v.reserve(4);
        v.emplace_back(3, 7);
        REQUIRE(v.size() == 1);
        CHECK_MESSAGE(v[0] == std::vector<int>({7, 7, 7}),
                      "emplace_back(3, 7) on a vector of std::vector<int> must call std::vector<int>(3, 7) - three "
                      "7s - but the element has size " << v[0].size()
                          << "\n-> construct with parentheses: std::construct_at(p, std::forward<Args>(args)...) "
                             "or ::new (p) T(std::forward<Args>(args)...), not T{...}.");
    }
}

// ------------------------------------------------------------------- pop_back

CPPATLAS_TEST("pop_back destroys exactly the last element, keeping capacity and the other elements") {
    vector<Tracked> v;
    emplace_values(v, 0, 5);
    REQUIRE(v.size() == 5);
    const Snapshot before = snapshot(v);
    CounterScope scope;
    v.pop_back();
    const Counts d = scope.delta();
    VERIFY(expect::ops(d, Op::destroy, 1, "pop_back destroys exactly one element: std::destroy_at(data_ + size_ - 1)"));
    CHECK_MESSAGE(d.element_ops() == 1, "pop_back did more than destroy the last element: " << to_string(d)
                                                                                         << "\n-> nothing needs to be "
                                                                                            "moved or reallocated.");
    REQUIRE(v.size() == 4);
    CHECK_MESSAGE(v.capacity() == before.capacity, "pop_back must not change capacity(): " << before.capacity << " -> "
                                                                                           << v.capacity());
    CHECK_MESSAGE((&v[0] == before.first), "pop_back must not reallocate");
    for (std::size_t i = 0; i < 4; ++i) {
        if (v[i].id() != before.ids[i] || !(v[i] == before.values[i])) {
            FAIL_CHECK("after pop_back, v[" << i << "] is " << v[i] << "; only the LAST element may be removed");
            break;
        }
    }
    VERIFY(expect::live(4));
}

CPPATLAS_TEST("pop_back until empty, then reuse the vector") {
    vector<Tracked> v;
    emplace_values(v, 0, 6);
    REQUIRE(v.size() == 6);
    const std::size_t cap = v.capacity();
    for (int i = 6; i > 0; --i) {
        REQUIRE(v.size() == static_cast<std::size_t>(i));
        CHECK(v[static_cast<std::size_t>(i) - 1] == i - 1);
        v.pop_back();
    }
    CHECK(v.size() == 0);
    CHECK(v.empty());
    CHECK(v.capacity() == cap);
    VERIFY(expect::live(0, "every element was popped"));
    v.emplace_back(10);
    REQUIRE(v.size() == 1);
    CHECK(v[0] == 10);
}

// ---------------------------------------------------------------------- clear

CPPATLAS_TEST("clear destroys every element, keeps capacity, and the vector stays usable") {
    vector<Tracked> v;
    emplace_values(v, 0, 10);
    REQUIRE(v.size() == 10);
    const std::size_t cap = v.capacity();
    CounterScope scope;
    v.clear();
    const Counts d = scope.delta();
    VERIFY(expect::ops(d, Op::destroy, 10, "clear destroys each of the size() elements exactly once"));
    CHECK(d.element_ops() == 10);
    CHECK(v.size() == 0);
    CHECK(v.empty());
    CHECK_MESSAGE(v.capacity() == cap,
                  "clear() must keep the storage: capacity() went from " << cap << " to " << v.capacity());
    VERIFY(expect::live(0));

    // Reuse: pushes up to the old capacity need no reallocation.
    scope.restart();
    emplace_values(v, 100, static_cast<int>(cap));
    REQUIRE(v.size() == cap);
    CHECK(v.capacity() == cap);
    CHECK(first_mismatch(v, 100, static_cast<int>(cap)) == -1);
    CHECK_MESSAGE(scope.delta().moves() + scope.delta().copies() == 0,
                  "refilling a cleared vector up to its capacity relocated elements");
}

CPPATLAS_TEST("clear on an empty vector does nothing; clear is noexcept") {
    vector<Tracked> v;
    CounterScope scope;
    v.clear();
    VERIFY(expect::no_element_ops(scope.delta(), "clear() on an empty vector"));
    CHECK(v.size() == 0);
    INFO("declare `void clear() noexcept`: destroying elements cannot throw");
    CHECK(noexcept(v.clear()));
}
