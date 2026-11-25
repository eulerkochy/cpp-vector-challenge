// Stage 1 - empty-vector
// Spec: https://cppatlas.com/build/vector/empty-vector
//
// A default-constructed vector: size() == 0, capacity() >= size(), empty().
//
// ---------------------------------------------------------------------------
// THE STAGE FILE PATTERN (stages 3-8 copy this; details in tests/support/README.md)
//
//   #include "support/harness.hpp"
//   using namespace cppatlas;
//
//   namespace preflight {            // 1. one check per member the tests use
//   CPPATLAS_PREFLIGHT(has_reserve,
//       "stage 3 (growth): my::vector<T> needs `void reserve(std::size_t new_cap)`.",
//       my::vector<Tracked>,
//       requires(V& v, std::size_t n) { v.reserve(n); });
//   }
//   CPPATLAS_STAGE_GATE(preflight::has_reserve && ...);   // 2. AND them all
//
//   CPPATLAS_TEST("reserve grows capacity") {             // 3. gated test bodies
//       vector<Tracked> v;      // `vector` == my::vector, but DEPENDENT: never
//       v.reserve(10);          // compiled when a preflight check failed
//       CHECK(v.capacity() >= 10);
//   }
//
// Missing member => the learner sees ONLY the static_assert message(s).
// ---------------------------------------------------------------------------
#include "support/harness.hpp"

using namespace cppatlas;

namespace preflight {

CPPATLAS_PREFLIGHT(default_constructible,
                   "stage 1 (empty-vector): my::vector<T> needs a default constructor `vector()` "
                   "(and a destructor).",
                   my::vector<int>, std::default_initializable<V> && std::destructible<V>);

CPPATLAS_PREFLIGHT(has_size,
                   "stage 1 (empty-vector): my::vector<T> needs `std::size_t size() const` "
                   "(a const member function, so it also works on const vectors).",
                   my::vector<int>,
                   requires(const V& v) {
                       { v.size() } -> std::convertible_to<std::size_t>;
                   });

CPPATLAS_PREFLIGHT(has_capacity,
                   "stage 1 (empty-vector): my::vector<T> needs `std::size_t capacity() const`.",
                   my::vector<int>,
                   requires(const V& v) {
                       { v.capacity() } -> std::convertible_to<std::size_t>;
                   });

CPPATLAS_PREFLIGHT(has_empty,
                   "stage 1 (empty-vector): my::vector<T> needs `bool empty() const`.",
                   my::vector<int>,
                   requires(const V& v) {
                       { v.empty() } -> std::convertible_to<bool>;
                   });

}  // namespace preflight

CPPATLAS_STAGE_GATE(preflight::default_constructible && preflight::has_size && preflight::has_capacity &&
                    preflight::has_empty);

CPPATLAS_TEST("a default-constructed vector has size() == 0") {
    vector<int> v;
    INFO("a vector that was just created holds no elements, so size() must return 0");
    CHECK(v.size() == 0);
}

CPPATLAS_TEST("a default-constructed vector is empty()") {
    vector<int> v;
    INFO("empty() must be true exactly when size() == 0");
    CHECK(v.empty());
}

CPPATLAS_TEST("capacity() is never smaller than size()") {
    vector<int> v;
    INFO("capacity() counts allocated slots; it can be 0 for an empty vector, but never less than size()");
    CHECK(v.capacity() >= v.size());
}

CPPATLAS_TEST("size(), capacity() and empty() work on a const vector") {
    const vector<int> v{};
    CHECK(v.size() == 0);
    CHECK(v.empty());
    CHECK(v.capacity() >= v.size());
}

CPPATLAS_TEST("size() returns an unsigned type") {
    vector<int> v;
    INFO("std::vector::size() returns size_type, an unsigned integer type (std::size_t)");
    CHECK(std::is_unsigned_v<decltype(v.size())>);
}

CPPATLAS_TEST("the default constructor is noexcept") {
    INFO("creating an empty vector cannot fail: declare `vector() noexcept` (std::vector's is noexcept too). "
         "If yours can throw, it probably allocates - an empty vector should not need any storage.");
    CHECK(std::is_nothrow_default_constructible_v<vector<int>>);
}

CPPATLAS_TEST("an empty vector creates no elements") {
    CounterScope scope;
    {
        vector<Tracked> v;
        CHECK(v.empty());
    }
    VERIFY(expect::no_element_ops(scope.delta(), "creating and destroying an empty vector<Tracked>"));
}

CPPATLAS_TEST("vector<T> of a type without a default constructor can be created") {
    // NoDefault has no default constructor. If this test fails to COMPILE, your
    // vector default-constructs T somewhere (e.g. `new T[n]`).
    vector<NoDefault> v;
    CHECK(v.size() == 0);
    CHECK(v.empty());
}

CPPATLAS_TEST("vectors of different element types are all empty") {
    vector<std::string> strings;
    vector<double> doubles;
    vector<Tracked> tracked;
    CHECK(strings.size() == 0);
    CHECK(doubles.size() == 0);
    CHECK(tracked.size() == 0);
    CHECK(strings.empty());
    CHECK(doubles.empty());
    CHECK(tracked.empty());
}
