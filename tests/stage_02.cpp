// Stage 2 - push-back
// Spec: https://cppatlas.com/build/vector/push-back
//
// Raw storage + push_back(const T&) + operator[] (const and non-const) + a
// destructor that destroys each element exactly once and frees the storage.
// Any growth strategy is accepted here (stage 3 makes it geometric), but
// push_back must work for any number of elements.
//
// Pattern: see the header of stage_01.cpp and tests/support/README.md.
// Note how every test REQUIREs the size before indexing, and loops over the
// EXPECTED count, never over v.size(): a wrong implementation must produce a
// test failure, never an out-of-bounds access.
#include "support/harness.hpp"

#include <cstdint>
#include <string>

using namespace cppatlas;

namespace preflight {

// Stage 2 tests also use the stage 1 API, so it is re-checked here.
CPPATLAS_PREFLIGHT(stage1_api,
                   "stage 2 (push-back) builds on stage 1: my::vector<T> needs `vector()`, "
                   "`size() const`, `capacity() const` and `empty() const` (run `./cppatlas test 1`).",
                   my::vector<Tracked>,
                   std::default_initializable<V> && requires(const V& v) {
                       { v.size() } -> std::convertible_to<std::size_t>;
                       { v.capacity() } -> std::convertible_to<std::size_t>;
                       { v.empty() } -> std::convertible_to<bool>;
                   });

CPPATLAS_PREFLIGHT(has_push_back,
                   "stage 2 (push-back): my::vector<T> needs `void push_back(const T& value)`.",
                   my::vector<Tracked>,
                   requires(V& v, const Tracked& t) { v.push_back(t); });

CPPATLAS_PREFLIGHT(has_index,
                   "stage 2 (push-back): my::vector<T> needs `T& operator[](std::size_t index)` "
                   "(returning a reference: T& - not a copy).",
                   my::vector<Tracked>,
                   requires(V& v, std::size_t i) {
                       { v[i] } -> std::same_as<Tracked&>;
                   });

CPPATLAS_PREFLIGHT(has_const_index,
                   "stage 2 (push-back): my::vector<T> needs a const overload too: "
                   "`const T& operator[](std::size_t index) const` (returning const T& - not a copy).",
                   my::vector<Tracked>,
                   requires(const V& v, std::size_t i) {
                       { v[i] } -> std::same_as<const Tracked&>;
                   });

}  // namespace preflight

CPPATLAS_STAGE_GATE(preflight::stage1_api && preflight::has_push_back && preflight::has_index &&
                    preflight::has_const_index);

namespace {

// Pushes 0, 1, ..., n-1 (as Elem(i)) and fails the test at the first push that
// does not increase size() by exactly one.
template <class V>
void push_sequence(V& v, int n) {
    using Elem = std::remove_cvref_t<decltype(v[0])>;
    for (int i = 0; i < n; ++i) {
        const Elem e(i);
        v.push_back(e);
        if (v.size() != static_cast<std::size_t>(i) + 1) {
            FAIL("after " << i + 1 << " push_back calls size() is " << v.size() << ", expected " << i + 1
                          << "\n-> push_back must add exactly one element (and grow the storage when "
                             "size() == capacity()).");
        }
    }
}

// Index of the first element that is not i (compared with ==), or -1.
template <class V>
long first_mismatch(const V& v, int n) {
    for (int i = 0; i < n; ++i)
        if (!(v[static_cast<std::size_t>(i)] == i)) return i;
    return -1;
}

}  // namespace

CPPATLAS_TEST("push_back on an empty vector stores one element") {
    vector<int> v;
    v.push_back(42);
    INFO("after one push_back(42): size() == 1, empty() == false, capacity() >= 1");
    REQUIRE(v.size() == 1);
    CHECK_FALSE(v.empty());
    CHECK(v.capacity() >= 1);
    CHECK(v[0] == 42);
}

CPPATLAS_TEST("push_back appends in order (a few elements)") {
    vector<int> v;
    v.push_back(10);
    v.push_back(20);
    v.push_back(30);
    REQUIRE(v.size() == 3);
    CHECK(v[0] == 10);
    CHECK(v[1] == 20);
    CHECK(v[2] == 30);
    CHECK(v.capacity() >= v.size());
}

CPPATLAS_TEST("push_back keeps every element when the storage grows (100 elements)") {
    vector<int> v;
    push_sequence(v, 100);
    REQUIRE(v.size() == 100);
    CHECK(v.capacity() >= 100);
    const long bad = first_mismatch(v, 100);
    CHECK_MESSAGE(bad == -1, "v[" << bad << "] == " << (bad >= 0 ? v[static_cast<std::size_t>(bad)] : 0)
                                  << ", expected " << bad
                                  << "\n-> when push_back needs more room it must copy (or move) every existing "
                                     "element into the new buffer before releasing the old one.");
}

CPPATLAS_TEST("operator[] returns a reference to the stored element") {
    vector<int> v;
    v.push_back(1);
    v.push_back(2);
    v.push_back(3);
    REQUIRE(v.size() == 3);

    CHECK_MESSAGE((std::is_same_v<decltype(v[0]), int&>), "non-const operator[] must return T& (here: int&)");

    v[1] = 20;
    INFO("writing through v[1] must change the element stored in the vector");
    CHECK(v[1] == 20);
    CHECK(v[0] == 1);
    CHECK(v[2] == 3);
    CHECK_MESSAGE(&v[1] == &v[0] + 1, "elements must live next to each other in one contiguous array");
    CHECK_MESSAGE(&v[2] == &v[0] + 2, "elements must live next to each other in one contiguous array");
}

CPPATLAS_TEST("const operator[] reads elements of a const vector") {
    vector<int> v;
    v.push_back(7);
    v.push_back(8);
    REQUIRE(v.size() == 2);
    const auto& cv = v;
    CHECK_MESSAGE((std::is_same_v<decltype(cv[0]), const int&>),
                  "operator[] const must return const T& (here: const int&)");
    CHECK(cv[0] == 7);
    CHECK(cv[1] == 8);
    CHECK_MESSAGE(&cv[0] == &v[0], "both overloads must refer to the same stored element");
}

CPPATLAS_TEST("push_back(const T&) copy-constructs the new element in place") {
    vector<Tracked> v;
    const Tracked original(5);
    CounterScope scope;
    v.push_back(original);
    const Counts d = scope.delta();
    REQUIRE(v.size() == 1);
    CHECK(v[0] == 5);
    CHECK_MESSAGE(original == 5, "push_back(const T&) must leave its argument untouched; it is now " << original);
    VERIFY(expect::no_default_constructions(d));
    VERIFY(expect::ops(d, Op::copy_construct, 1,
                       "push_back(const T&) into an empty vector should copy-construct exactly one element: "
                       "std::construct_at(slot, value) or `new (slot) T(value)`. Assigning into raw storage "
                       "(`data_[n] = value`) is not construction."));
    CHECK_MESSAGE(d.assignments() == 0, "no element should be assigned during push_back, saw " << to_string(d));
}

CPPATLAS_TEST("storage is allocated without default-constructing T") {
    CounterScope scope;
    {
        vector<Tracked> v;
        push_sequence(v, 40);
        REQUIRE(v.size() == 40);
        CHECK(first_mismatch(v, 40) == -1);
    }
    VERIFY(expect::no_default_constructions(scope.delta()));
}

CPPATLAS_TEST("works for an element type without a default constructor") {
    // If this file fails to COMPILE with "NoDefault ... deleted", your vector
    // default-constructs T (e.g. `new T[n]`): use raw storage instead.
    vector<NoDefault> v;
    push_sequence(v, 20);
    REQUIRE(v.size() == 20);
    const long bad = first_mismatch(v, 20);
    CHECK_MESSAGE(bad == -1, "element " << bad << " has the wrong value");
}

CPPATLAS_TEST("the destructor destroys every element exactly once") {
    CounterScope scope;
    {
        vector<Tracked> v;
        push_sequence(v, 25);
        REQUIRE(v.size() == 25);
        VERIFY(expect::live(25, "a vector of 25 elements should own exactly 25 live objects: relocating into "
                                "a new buffer must destroy the old elements"));
    }
    VERIFY(expect::all_destroyed());
    VERIFY(expect::lifetime_clean());
    VERIFY(expect::destroyed_exactly_once(scope.delta()));
}

CPPATLAS_TEST("an empty vector and a vector with one element are destroyed cleanly") {
    {
        vector<Tracked> empty;
        (void)empty;
    }
    {
        vector<Tracked> one;
        one.push_back(Tracked(1));
        REQUIRE(one.size() == 1);
    }
    VERIFY(expect::all_destroyed());
    VERIFY(expect::lifetime_clean());
}

CPPATLAS_TEST("works with std::string (a type that owns heap memory)") {
    vector<std::string> v;
    const int n = 30;
    for (int i = 0; i < n; ++i) {
        // Long enough to defeat the small-string optimisation, so the strings
        // own heap memory (AddressSanitizer and LeakSanitizer check it).
        v.push_back(std::string(40, static_cast<char>('a' + i % 26)) + std::to_string(i));
        REQUIRE(v.size() == static_cast<std::size_t>(i) + 1);
    }
    for (int i = 0; i < n; ++i) {
        const std::string expected = std::string(40, static_cast<char>('a' + i % 26)) + std::to_string(i);
        if (v[static_cast<std::size_t>(i)] != expected) {
            FAIL_CHECK("v[" << i << "] is \"" << v[static_cast<std::size_t>(i)] << "\", expected \"" << expected
                            << "\"\n-> relocate elements with their copy/move constructor, not memcpy: "
                               "memcpy is not portable (undefined behaviour) for types that are not trivially "
                               "relocatable, and std::string is not on every library.");
            break;
        }
    }
}

CPPATLAS_TEST("two vectors do not share storage") {
    vector<int> a;
    vector<int> b;
    a.push_back(1);
    b.push_back(2);
    a.push_back(3);
    REQUIRE(a.size() == 2);
    REQUIRE(b.size() == 1);
    CHECK(a[0] == 1);
    CHECK(a[1] == 3);
    CHECK(b[0] == 2);
    CHECK_MESSAGE(&a[0] != &b[0], "each vector must own its own buffer (no static/shared storage)");
}

CPPATLAS_TEST("works with short std::string elements (small-string optimisation)") {
    // Short strings live INSIDE the std::string object; libstdc++'s even holds
    // a pointer into itself. Relocating them with memcpy leaves that pointer
    // aimed at the old buffer.
    vector<std::string> v;
    const int n = 100;
    for (int i = 0; i < n; ++i) {
        v.push_back(std::to_string(i));
        REQUIRE(v.size() == static_cast<std::size_t>(i) + 1);
    }
    for (int i = 0; i < n; ++i) {
        if (v[static_cast<std::size_t>(i)] != std::to_string(i)) {
            FAIL_CHECK("v[" << i << "] is \"" << v[static_cast<std::size_t>(i)] << "\", expected \"" << i
                            << "\" after the storage grew"
                            << "\n-> relocate elements with their copy/move constructor (std::construct_at(new_data "
                               "+ i, old[i])), never memcpy: memcpy is not portable (undefined behaviour) for types "
                               "that are not trivially relocatable.");
            break;
        }
    }
}

namespace {
// Over-aligned element: new T's storage must respect alignof(T) == 64.
struct alignas(64) Wide {
    int x = 0;
    explicit Wide(int v) : x(v) {}
    bool operator==(int v) const { return x == v; }
};
}  // namespace

CPPATLAS_TEST("elements of an over-aligned type (alignas(64)) are suitably aligned") {
    for (int round = 0; round < 8; ++round) {
        vector<Wide> v;
        for (int i = 0; i < 40; ++i) {
            v.push_back(Wide(i));
            REQUIRE(v.size() == static_cast<std::size_t>(i) + 1);
            const auto addr = reinterpret_cast<std::uintptr_t>(&v[0]);
            if (addr % alignof(Wide) != 0) {
                FAIL("&v[0] == " << static_cast<const void*>(&v[0]) << " is not a multiple of alignof(T) == "
                                 << alignof(Wide)
                                 << "\n-> allocate with std::allocator<T>().allocate(n), or with ::operator new(n * "
                                    "sizeof(T), std::align_val_t{alignof(T)}) (and free with the matching "
                                    "::operator delete(p, std::align_val_t{alignof(T)})). Plain ::operator new / "
                                    "malloc only guarantee alignof(std::max_align_t).");
            }
        }
        REQUIRE(v.size() == 40);
        CHECK(first_mismatch(v, 40) == -1);
    }
}
