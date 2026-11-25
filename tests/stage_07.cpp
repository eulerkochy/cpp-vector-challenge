// Stage 7 - iterators
// Spec: https://cppatlas.com/build/vector/iterators
//
// Member types, begin/end/cbegin/cend (+ const overloads), data(), and the
// standard iterator/range concepts (std::contiguous_iterator,
// std::ranges::contiguous_range), so that range-for, <algorithm> and
// std::ranges work.
//
// Standard facts used ([vector.overview], [container.reqmts]):
//   - value_type = T, reference = T&, const_reference = const T&,
//     pointer = T* and const_pointer = const T* (allocator_traits of
//     std::allocator<T>), size_type is an unsigned integer type and
//     difference_type a signed one, equal to the iterators' difference type.
//   - iterator / const_iterator are contiguous iterators whose reference types
//     are T& / const T&; iterator converts to const_iterator (not the reverse)
//     and the two compare with ==.
//   - begin() == end() for an empty container; end() - begin() == size().
//   - data(): [data(), data() + size()) is a valid range, and for a non-empty
//     vector data() == addressof(front()).
//   - After reserve(n), insertions do not invalidate iterators until size()
//     exceeds capacity().
// Under USE_STD the iterator is a class type, so the tests never assume T*;
// they use std::to_address to compare an iterator with a pointer.
// Reverse iterators are not tested.
#include "support/harness.hpp"

#include <algorithm>
#include <functional>
#include <iterator>
#include <memory>
#include <numeric>
#include <ranges>
#include <span>

using namespace cppatlas;

namespace preflight {

CPPATLAS_PREFLIGHT(stage1_api,
                   "stage 7 (iterators) builds on stage 1: my::vector<T> needs `vector()`, "
                   "`size() const`, `capacity() const` and `empty() const` (run `./cppatlas test 1`).",
                   my::vector<int>,
                   std::default_initializable<V> && requires(const V& v) {
                       { v.size() } -> std::convertible_to<std::size_t>;
                       { v.capacity() } -> std::convertible_to<std::size_t>;
                       { v.empty() } -> std::convertible_to<bool>;
                   });

CPPATLAS_PREFLIGHT(earlier_api,
                   "stage 7 (iterators) builds on stages 2-5: my::vector<T> needs `push_back(const T&)`, "
                   "`push_back(T&&)`, `operator[]` (const and non-const), `reserve(std::size_t)` and "
                   "`emplace_back(Args&&...)`.",
                   my::vector<Tracked>,
                   requires(V& v, const V& cv, const Tracked& t, std::size_t i) {
                       v.push_back(t);
                       v.push_back(Tracked(1));
                       { v[i] } -> std::same_as<Tracked&>;
                       { cv[i] } -> std::same_as<const Tracked&>;
                       v.reserve(i);
                       v.emplace_back(1);
                   });

CPPATLAS_PREFLIGHT(has_value_type, "stage 7 (iterators): my::vector<T> needs `using value_type = T;`.",
                   my::vector<int>, requires { typename V::value_type; });
CPPATLAS_PREFLIGHT(has_size_type, "stage 7 (iterators): my::vector<T> needs `using size_type = std::size_t;`.",
                   my::vector<int>, requires { typename V::size_type; });
CPPATLAS_PREFLIGHT(has_difference_type,
                   "stage 7 (iterators): my::vector<T> needs `using difference_type = std::ptrdiff_t;`.",
                   my::vector<int>, requires { typename V::difference_type; });
CPPATLAS_PREFLIGHT(has_reference, "stage 7 (iterators): my::vector<T> needs `using reference = T&;`.",
                   my::vector<int>, requires { typename V::reference; });
CPPATLAS_PREFLIGHT(has_const_reference,
                   "stage 7 (iterators): my::vector<T> needs `using const_reference = const T&;`.",
                   my::vector<int>, requires { typename V::const_reference; });
CPPATLAS_PREFLIGHT(has_pointer, "stage 7 (iterators): my::vector<T> needs `using pointer = T*;`.",
                   my::vector<int>, requires { typename V::pointer; });
CPPATLAS_PREFLIGHT(has_const_pointer,
                   "stage 7 (iterators): my::vector<T> needs `using const_pointer = const T*;`.",
                   my::vector<int>, requires { typename V::const_pointer; });
CPPATLAS_PREFLIGHT(has_iterator, "stage 7 (iterators): my::vector<T> needs `using iterator = T*;`.",
                   my::vector<int>, requires { typename V::iterator; });
CPPATLAS_PREFLIGHT(has_const_iterator,
                   "stage 7 (iterators): my::vector<T> needs `using const_iterator = const T*;`.",
                   my::vector<int>, requires { typename V::const_iterator; });

CPPATLAS_PREFLIGHT(has_begin_end,
                   "stage 7 (iterators): my::vector<T> needs `iterator begin() noexcept` and "
                   "`iterator end() noexcept`.",
                   my::vector<int>,
                   requires(V& v) {
                       v.begin();
                       v.end();
                   });
CPPATLAS_PREFLIGHT(has_const_begin_end,
                   "stage 7 (iterators): my::vector<T> needs the const overloads `const_iterator begin() const "
                   "noexcept` and `const_iterator end() const noexcept`.",
                   my::vector<int>,
                   requires(const V& v) {
                       v.begin();
                       v.end();
                   });
CPPATLAS_PREFLIGHT(has_cbegin_cend,
                   "stage 7 (iterators): my::vector<T> needs `const_iterator cbegin() const noexcept` and "
                   "`const_iterator cend() const noexcept`.",
                   my::vector<int>,
                   requires(const V& v) {
                       v.cbegin();
                       v.cend();
                   });
CPPATLAS_PREFLIGHT(has_data,
                   "stage 7 (iterators): my::vector<T> needs `T* data() noexcept` and `const T* data() const "
                   "noexcept`.",
                   my::vector<int>,
                   requires(V& v, const V& cv) {
                       v.data();
                       cv.data();
                   });

}  // namespace preflight

CPPATLAS_STAGE_GATE(preflight::stage1_api && preflight::earlier_api && preflight::has_value_type &&
                    preflight::has_size_type && preflight::has_difference_type && preflight::has_reference &&
                    preflight::has_const_reference && preflight::has_pointer && preflight::has_const_pointer &&
                    preflight::has_iterator && preflight::has_const_iterator && preflight::has_begin_end &&
                    preflight::has_const_begin_end && preflight::has_cbegin_cend && preflight::has_data);

namespace {

// 0, 1, ..., n-1 via push_back; fails at the first push that does not grow size by one.
template <class V>
void push_ints(V& v, int n) {
    for (int i = 0; i < n; ++i) {
        v.push_back(i);
        if (v.size() != static_cast<std::size_t>(i) + 1) FAIL("push_back #" << i + 1 << " did not add one element");
    }
}

}  // namespace

// --------------------------------------------------------------- member types

CPPATLAS_TEST("member types: value_type, reference, pointer, size_type, difference_type") {
    using V = vector<Tracked>;
    CHECK_MESSAGE((std::is_same_v<typename V::value_type, Tracked>), "value_type must be T");
    CHECK_MESSAGE((std::is_same_v<typename V::reference, Tracked&>), "reference must be T&");
    CHECK_MESSAGE((std::is_same_v<typename V::const_reference, const Tracked&>), "const_reference must be const T&");
    CHECK_MESSAGE((std::is_same_v<typename V::pointer, Tracked*>), "pointer must be T*");
    CHECK_MESSAGE((std::is_same_v<typename V::const_pointer, const Tracked*>), "const_pointer must be const T*");
    CHECK_MESSAGE((std::is_unsigned_v<typename V::size_type> && std::is_integral_v<typename V::size_type>),
                  "size_type must be an unsigned integer type (std::size_t)");
    CHECK_MESSAGE((std::is_signed_v<typename V::difference_type> && std::is_integral_v<typename V::difference_type>),
                  "difference_type must be a signed integer type (std::ptrdiff_t)");
    CHECK_MESSAGE((std::is_same_v<decltype(std::declval<const V&>().size()), typename V::size_type>),
                  "size() must return size_type");
}

CPPATLAS_TEST("begin/end/cbegin/cend/data return the right types") {
    using V = vector<int>;
    using It = typename V::iterator;
    using CIt = typename V::const_iterator;
    V v;
    const V& cv = v;
    CHECK_MESSAGE((std::is_same_v<decltype(v.begin()), It>), "begin() on a non-const vector must return iterator");
    CHECK_MESSAGE((std::is_same_v<decltype(v.end()), It>), "end() on a non-const vector must return iterator");
    CHECK_MESSAGE((std::is_same_v<decltype(cv.begin()), CIt>), "begin() const must return const_iterator");
    CHECK_MESSAGE((std::is_same_v<decltype(cv.end()), CIt>), "end() const must return const_iterator");
    CHECK_MESSAGE((std::is_same_v<decltype(v.cbegin()), CIt>),
                  "cbegin() must return const_iterator, even on a non-const vector");
    CHECK_MESSAGE((std::is_same_v<decltype(v.cend()), CIt>), "cend() must return const_iterator");
    CHECK_MESSAGE((std::is_same_v<decltype(v.data()), int*>), "data() must return T*");
    CHECK_MESSAGE((std::is_same_v<decltype(cv.data()), const int*>), "data() const must return const T*");
    CHECK_MESSAGE((std::is_same_v<std::iter_reference_t<It>, int&>), "*iterator must be T&");
    CHECK_MESSAGE((std::is_same_v<std::iter_reference_t<CIt>, const int&>), "*const_iterator must be const T&");
    CHECK_MESSAGE((std::is_same_v<std::iter_value_t<It>, int>), "the iterator's value type must be T");
    CHECK_MESSAGE((std::is_same_v<std::iter_difference_t<It>, typename V::difference_type>),
                  "the iterator's difference type must be the vector's difference_type");
}

// ---------------------------------------------------------- concepts (compile)

CPPATLAS_TEST("iterator and const_iterator are contiguous iterators") {
    using V = vector<int>;
    CHECK_MESSAGE((std::contiguous_iterator<typename V::iterator>),
                  "std::contiguous_iterator<iterator> must hold: `using iterator = T*;` is the simplest way");
    CHECK_MESSAGE((std::contiguous_iterator<typename V::const_iterator>),
                  "std::contiguous_iterator<const_iterator> must hold: `using const_iterator = const T*;`");
    CHECK((std::random_access_iterator<typename V::iterator>));
}

CPPATLAS_TEST("my::vector is a contiguous, sized, common range (also when const)") {
    using V = vector<int>;
    CHECK_MESSAGE((std::ranges::contiguous_range<V>), "std::ranges::contiguous_range<vector<T>> must hold");
    CHECK_MESSAGE((std::ranges::sized_range<V>), "std::ranges::sized_range<vector<T>> must hold");
    CHECK_MESSAGE((std::ranges::common_range<V>), "begin() and end() must return the same type (common_range)");
    CHECK_MESSAGE((std::ranges::contiguous_range<const V>),
                  "a const vector must be a contiguous range too: add const overloads of begin()/end()");
    CHECK((std::ranges::sized_range<const V>));
    CHECK((std::same_as<std::ranges::range_reference_t<const V>, const int&>));
}

CPPATLAS_TEST("a const vector and const_iterator give read-only access (compile-time check)") {
    constexpr bool write_through_const_begin = requires(const vector<int>& cv) { *cv.begin() = 1; };
    constexpr bool write_through_cbegin = requires(vector<int>& v) { *v.cbegin() = 1; };
    constexpr bool write_through_const_data = requires(const vector<int>& cv) { *cv.data() = 1; };
    CHECK_MESSAGE(!write_through_const_begin,
                  "`*cv.begin() = 1` compiles for a const vector cv\n-> begin() const must return const_iterator "
                  "(const T*), not iterator.");
    CHECK_MESSAGE(!write_through_cbegin, "`*v.cbegin() = 1` compiles\n-> cbegin() must return const_iterator.");
    CHECK_MESSAGE(!write_through_const_data, "`*cv.data() = 1` compiles\n-> data() const must return const T*.");
}

CPPATLAS_TEST("iterator converts to const_iterator, not the other way round") {
    using It = typename vector<int>::iterator;
    using CIt = typename vector<int>::const_iterator;
    CHECK_MESSAGE((std::convertible_to<It, CIt>), "an iterator must convert to a const_iterator");
    CHECK_MESSAGE((!std::convertible_to<CIt, It>),
                  "a const_iterator must NOT convert to an iterator (that would allow writing through it)");
    vector<int> v;
    push_ints(v, 3);
    REQUIRE(v.size() == 3);
    CIt c = v.begin();
    CHECK(c == v.cbegin());
    CHECK(v.begin() == v.cbegin());  // iterator and const_iterator compare
    CHECK(v.end() == v.cend());
}

// ------------------------------------------------------------- basic behavior

CPPATLAS_TEST("an empty vector: begin() == end()") {
    vector<int> v;
    const vector<int>& cv = v;
    CHECK(v.begin() == v.end());
    CHECK(cv.begin() == cv.end());
    CHECK(v.cbegin() == v.cend());
    CHECK(std::ranges::empty(v));
    CHECK(std::ranges::distance(v) == 0);
    int visited = 0;
    for (int x : v) {
        (void)x;
        ++visited;
    }
    CHECK(visited == 0);
}

CPPATLAS_TEST("end() - begin() == size(), and iterators point at the elements") {
    vector<int> v;
    push_ints(v, 10);
    REQUIRE(v.size() == 10);
    CHECK_MESSAGE(v.end() - v.begin() == 10, "end() must be begin() + size(), one past the last element");
    CHECK(std::distance(v.cbegin(), v.cend()) == 10);
    CHECK(std::ranges::size(v) == 10);
    CHECK_MESSAGE((v.data() == &v[0]), "data() must point at the first element");
    CHECK_MESSAGE((std::to_address(v.begin()) == v.data()), "begin() must refer to the first element (data())");
    CHECK_MESSAGE((std::to_address(v.end()) == v.data() + 10), "end() must be data() + size()");
    const vector<int>& cv = v;
    CHECK((cv.data() == &cv[0]));
    CHECK((std::to_address(cv.begin()) == cv.data()));
    bool ok = true;
    for (int i = 0; i < 10; ++i) ok = ok && (*(v.begin() + i) == i) && (std::to_address(v.begin() + i) == &v[static_cast<std::size_t>(i)]);
    CHECK_MESSAGE(ok, "begin() + i must refer to v[i]");
}

CPPATLAS_TEST("range-based for visits the elements in order (read and write, const and non-const)") {
    vector<int> v;
    push_ints(v, 8);
    REQUIRE(v.size() == 8);
    int expected = 0;
    bool in_order = true;
    for (const int& x : v) in_order = in_order && (x == expected++);
    CHECK_MESSAGE(in_order, "range-for did not visit 0, 1, ..., 7 in order");
    CHECK(expected == 8);
    for (int& x : v) x *= 10;
    CHECK(v[0] == 0);
    CHECK(v[7] == 70);
    const vector<int>& cv = v;
    int sum = 0;
    for (int x : cv) sum += x;
    CHECK(sum == 280);
    *v.begin() = 5;
    CHECK(v[0] == 5);
}

CPPATLAS_TEST("range-for over Tracked elements creates no copies") {
    vector<Tracked> v;
    v.reserve(5);
    for (int i = 0; i < 5; ++i) v.emplace_back(i);
    REQUIRE(v.size() == 5);
    CounterScope scope;
    long sum = 0;
    for (const Tracked& t : v) sum += t.value();
    VERIFY(expect::no_element_ops(scope.delta(), "iterating with range-for by reference",
                                  "*it must return a reference to the stored element (T&), not a copy"));
    CHECK(sum == 10);
}

// ------------------------------------------------------------------ algorithms

CPPATLAS_TEST("<algorithm> and <numeric>: sort, find, accumulate, reverse, count") {
    vector<int> v;
    for (int x : {5, 3, 9, 1, 7, 3}) v.push_back(x);
    REQUIRE(v.size() == 6);
    CHECK(std::accumulate(v.begin(), v.end(), 0) == 28);
    CHECK(std::count(v.cbegin(), v.cend(), 3) == 2);
    auto it = std::find(v.begin(), v.end(), 9);
    REQUIRE(it != v.end());
    CHECK(it - v.begin() == 2);
    CHECK(std::find(v.begin(), v.end(), 42) == v.end());
    std::sort(v.begin(), v.end());
    const int sorted[] = {1, 3, 3, 5, 7, 9};
    CHECK(std::equal(v.begin(), v.end(), std::begin(sorted), std::end(sorted)));
    std::reverse(v.begin(), v.end());
    CHECK(v[0] == 9);
    CHECK(v[5] == 1);
    CHECK(*std::max_element(v.cbegin(), v.cend()) == 9);
}

CPPATLAS_TEST("std::ranges algorithms and views work on my::vector") {
    vector<int> v;
    for (int x : {4, 8, 15, 16, 23, 42}) v.push_back(x);
    REQUIRE(v.size() == 6);
    std::ranges::sort(v, std::ranges::greater{});
    CHECK(v[0] == 42);
    CHECK(v[5] == 4);
    auto it = std::ranges::find(v, 15);
    REQUIRE(it != v.end());
    CHECK(*it == 15);
    CHECK(std::ranges::count_if(v, [](int x) { return x % 2 == 0; }) == 4);
    int sum = 0;
    for (int x : v | std::views::filter([](int x) { return x > 10; }) | std::views::transform([](int x) { return x * 2; }))
        sum += x;
    CHECK(sum == 2 * (42 + 23 + 16 + 15));
    const vector<int>& cv = v;
    CHECK(std::ranges::distance(cv | std::views::take(3)) == 3);
    std::span<const int> s(cv.data(), cv.size());
    CHECK(s.size() == 6);
}

CPPATLAS_TEST("std::sort on Tracked elements keeps every element alive exactly once") {
    vector<Tracked> v;
    for (int x : {3, 1, 2, 5, 4}) v.emplace_back(x);
    REQUIRE(v.size() == 5);
    std::sort(v.begin(), v.end(), [](const Tracked& a, const Tracked& b) { return a.value() < b.value(); });
    bool sorted = true;
    for (int i = 0; i < 5; ++i) sorted = sorted && (v[static_cast<std::size_t>(i)] == i + 1);
    CHECK(sorted);
    VERIFY(expect::live(5));
}

// ------------------------------------------------------------- invalidation

CPPATLAS_TEST("iterators stay valid across push_back when the capacity suffices (after reserve)") {
    vector<int> v;
    v.reserve(32);
    push_ints(v, 4);
    REQUIRE(v.size() == 4);
    REQUIRE(v.capacity() >= 32);
    const auto first = v.begin();
    const auto third = v.begin() + 2;
    for (int i = 4; i < 32; ++i) v.push_back(i);
    REQUIRE(v.size() == 32);
    CHECK_MESSAGE((first == v.begin()), "begin() changed although no reallocation was needed");
    CHECK(*first == 0);
    CHECK(*third == 2);
    CHECK(v.end() - first == 32);
}
