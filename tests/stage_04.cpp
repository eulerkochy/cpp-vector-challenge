// Stage 4 - copy-and-move
// Spec: https://cppatlas.com/build/vector/copy-and-move
//
// push_back(T&&), and the rule of five: copy/move construction and assignment.
//
// Standard facts used:
//   - push_back(T&&) constructs the element from std::move(x): one move
//     construction when no reallocation is needed ([sequence.reqmts]).
//   - vector(vector&&) is noexcept and constant time; operator=(vector&&) is
//     noexcept for std::allocator ([vector.overview]) and constant time, and
//     "all existing elements of the target are either move assigned to or
//     destroyed" ([container.alloc.reqmts]); with std::allocator (propagating)
//     they are destroyed - nothing is move-assigned.
//   - copy construction produces an equal vector ([container.reqmts]); every
//     implementation does it with exactly size() copy constructions.
//   - the source of a move ASSIGNMENT is "valid but unspecified": the tests
//     only destroy it, assign to it and push_back into it. The source of a move
//     CONSTRUCTION must be empty: no element was constructed and the buffer now
//     belongs to the new vector, so it cannot hold any element.
// Self-move-assignment is not tested. Copying a vector<MoveOnly> is not required
// to compile (and is not tested either way: std::vector<MoveOnly> claims to be
// copy constructible, it only fails when the copy is instantiated).
#include "support/harness.hpp"

using namespace cppatlas;

namespace preflight {

CPPATLAS_PREFLIGHT(stage1_api,
                   "stage 4 (copy-and-move) builds on stage 1: my::vector<T> needs `vector()`, "
                   "`size() const`, `capacity() const` and `empty() const` (run `./cppatlas test 1`).",
                   my::vector<Tracked>,
                   std::default_initializable<V> && requires(const V& v) {
                       { v.size() } -> std::convertible_to<std::size_t>;
                       { v.capacity() } -> std::convertible_to<std::size_t>;
                       { v.empty() } -> std::convertible_to<bool>;
                   });

CPPATLAS_PREFLIGHT(stage2_api,
                   "stage 4 (copy-and-move) builds on stage 2: my::vector<T> needs `void push_back(const T&)`, "
                   "`T& operator[](std::size_t)` and `const T& operator[](std::size_t) const`.",
                   my::vector<Tracked>,
                   requires(V& v, const V& cv, const Tracked& t, std::size_t i) {
                       v.push_back(t);
                       { v[i] } -> std::same_as<Tracked&>;
                       { cv[i] } -> std::same_as<const Tracked&>;
                   });

CPPATLAS_PREFLIGHT(has_reserve,
                   "stage 4 (copy-and-move) builds on stage 3: my::vector<T> needs `void reserve(std::size_t)`.",
                   my::vector<Tracked>,
                   requires(V& v, std::size_t n) { v.reserve(n); });

// An rvalue also binds to push_back(const T&), so a plain call cannot tell the
// two overloads apart: ask for the T&& overload by its exact signature.
CPPATLAS_PREFLIGHT(has_push_back_rvalue,
                   "stage 4 (copy-and-move): my::vector<T> needs the rvalue overload `void push_back(T&& value)`.",
                   my::vector<Tracked>,
                   requires { static_cast<void (V::*)(Tracked&&)>(&V::push_back); });

CPPATLAS_PREFLIGHT(copy_constructible,
                   "stage 4 (copy-and-move): my::vector<T> needs a copy constructor `vector(const vector& other)` "
                   "(remove the `= delete` from stage 2).",
                   my::vector<Tracked>, std::is_copy_constructible_v<V>);

CPPATLAS_PREFLIGHT(copy_assignable,
                   "stage 4 (copy-and-move): my::vector<T> needs copy assignment "
                   "`vector& operator=(const vector& other)` (remove the `= delete` from stage 2).",
                   my::vector<Tracked>, std::is_copy_assignable_v<V>);

CPPATLAS_PREFLIGHT(move_constructible,
                   "stage 4 (copy-and-move): my::vector<T> needs a move constructor `vector(vector&& other) noexcept`.",
                   my::vector<Tracked>, std::is_move_constructible_v<V>);

CPPATLAS_PREFLIGHT(move_assignable,
                   "stage 4 (copy-and-move): my::vector<T> needs move assignment "
                   "`vector& operator=(vector&& other) noexcept`.",
                   my::vector<Tracked>, std::is_move_assignable_v<V>);

}  // namespace preflight

CPPATLAS_STAGE_GATE(preflight::stage1_api && preflight::stage2_api && preflight::has_reserve &&
                    preflight::has_push_back_rvalue && preflight::copy_constructible &&
                    preflight::copy_assignable && preflight::move_constructible && preflight::move_assignable);

namespace {

// Pushes Elem(first) .. Elem(first + n - 1) as rvalues; fails the test at the
// first push that does not add exactly one element.
template <class V>
void push_values(V& v, int first, int n) {
    using Elem = std::remove_cvref_t<decltype(v[0])>;
    const std::size_t start = v.size();
    for (int i = 0; i < n; ++i) {
        v.push_back(Elem(first + i));
        if (v.size() != start + static_cast<std::size_t>(i) + 1)
            FAIL("after " << i + 1 << " push_back calls size() is " << v.size() << ", expected "
                          << start + static_cast<std::size_t>(i) + 1);
    }
}

// Index of the first element that is not first+i, or -1.
template <class V>
long first_mismatch(const V& v, int first, int n) {
    for (int i = 0; i < n; ++i)
        if (!(v[static_cast<std::size_t>(i)] == first + i)) return i;
    return -1;
}

}  // namespace

// ------------------------------------------------------------- push_back(T&&)

CPPATLAS_TEST("push_back(T&&) move-constructs the new element (no copy)") {
    vector<Tracked> v;
    v.reserve(4);
    Tracked x(7);
    CounterScope scope;
    v.push_back(std::move(x));
    const Counts d = scope.delta();
    REQUIRE(v.size() == 1);
    CHECK(v[0] == 7);
    VERIFY(expect::no_copies(d, "push_back(std::move(x))",
                             "an rvalue must be moved in: add `void push_back(T&& value)` and construct the element "
                             "with std::move(value) - inside the function `value` is an lvalue, so without "
                             "std::move it is copied."));
    VERIFY(expect::ops(d, Op::move_construct, 1,
                       "with spare capacity, push_back(T&&) performs exactly one move construction."));
    CHECK_MESSAGE(d.element_ops() == 1, "push_back(T&&) with spare capacity did more than one move: " << to_string(d));

    scope.restart();
    v.push_back(Tracked(8));  // a prvalue: the temporary is constructed, moved from, destroyed
    const Counts t = scope.delta();
    REQUIRE(v.size() == 2);
    CHECK(v[1] == 8);
    VERIFY(expect::no_copies(t, "push_back(Tracked(8))", "a temporary is an rvalue: it must be moved, not copied."));
    VERIFY(expect::ops(t, Op::move_construct, 1));
}

CPPATLAS_TEST("push_back(const T&) still copies, and leaves the argument untouched") {
    vector<Tracked> v;
    v.reserve(4);
    const Tracked x(5);
    Tracked y(6);
    CounterScope scope;
    v.push_back(x);
    v.push_back(y);  // a non-const lvalue must be copied too, never moved from
    const Counts d = scope.delta();
    REQUIRE(v.size() == 2);
    VERIFY(expect::ops(d, Op::copy_construct, 2, "push_back of an lvalue copies it."));
    CHECK_MESSAGE(d.moves() == 0, "push_back of an lvalue moved something: " << to_string(d)
                                                                             << "\n-> only an rvalue may be moved from.");
    CHECK(x == 5);
    CHECK_MESSAGE(y == 6, "push_back(y) with a non-const lvalue y must not move from y; y is now " << y);
    CHECK(v[0] == 5);
    CHECK(v[1] == 6);
}

CPPATLAS_TEST("push_back(T&&) works for a move-only type, with reallocations (relocation must MOVE; if this does not compile, your reallocation still copies)") {
    vector<MoveOnly> v;
    push_values(v, 0, 50);
    REQUIRE(v.size() == 50);
    const long bad = first_mismatch(v, 0, 50);
    CHECK_MESSAGE(bad == -1, "element " << bad << " has the wrong value");
    MoveOnly m(50);
    v.push_back(std::move(m));
    REQUIRE(v.size() == 51);
    CHECK(v[50] == 50);
    VERIFY(expect::live(52, "51 elements in the vector + the moved-from local"));
}

// ------------------------------------------------------------ copy constructor

CPPATLAS_TEST("the copy constructor makes exactly size() copies and nothing else") {
    vector<Tracked> v;
    v.reserve(32);  // capacity() > size(): only size() elements may be copied
    push_values(v, 10, 10);
    REQUIRE(v.size() == 10);
    CounterScope scope;
    const vector<Tracked> c(v);
    const Counts d = scope.delta();
    VERIFY(expect::ops(d, Op::copy_construct, 10,
                       "copying a vector of 10 elements copy-constructs exactly 10 elements into fresh raw storage "
                       "(not capacity() elements, and no default construction + assignment)."));
    CHECK_MESSAGE(d.element_ops() == 10, "the copy constructor did more than copy 10 elements: " << to_string(d));
    REQUIRE(c.size() == 10);
    CHECK(c.capacity() >= c.size());
    CHECK(first_mismatch(c, 10, 10) == -1);
    CHECK(first_mismatch(v, 10, 10) == -1);
}

CPPATLAS_TEST("a copy is deep and independent of the original") {
    vector<Tracked> v;
    push_values(v, 0, 6);
    REQUIRE(v.size() == 6);
    vector<Tracked> c(v);
    REQUIRE(c.size() == 6);
    CHECK_MESSAGE((&c[0] != &v[0]), "the copy shares the original's buffer (a shallow copy)"
                                  "\n-> allocate a new buffer and copy-construct each element into it; copying only "
                                  "the pointer makes both vectors destroy the same elements.");
    c[0] = Tracked(100);
    c.push_back(Tracked(200));
    REQUIRE(v.size() == 6);
    CHECK_MESSAGE(v[0] == 0, "changing the copy changed the original: v[0] is " << v[0]);
    REQUIRE(c.size() == 7);
    CHECK(c[0] == 100);
    CHECK(c[6] == 200);
    v.push_back(Tracked(300));
    CHECK(c.size() == 7);
    VERIFY(expect::live(14, "two independent vectors of 7 elements each"));
}

CPPATLAS_TEST("copying an empty vector creates no elements") {
    vector<Tracked> v;
    CounterScope scope;
    vector<Tracked> c(v);
    VERIFY(expect::no_element_ops(scope.delta(), "copying an empty vector"));
    CHECK(c.size() == 0);
    CHECK(c.empty());
    c.push_back(Tracked(1));
    CHECK(c.size() == 1);
    CHECK(v.size() == 0);
}

// ------------------------------------------------------------ copy assignment

CPPATLAS_TEST("copy assignment: smaller <- larger (with and without spare capacity), larger <- smaller, empty") {
    struct Case {
        int target, source;
        int reserve;  // a.reserve(reserve) first: 0 = no reserve
    };
    // {2, 7, 16}: the target is smaller but already has room for the source's
    // elements (target.size() < source.size() <= target.capacity()), so a
    // correct assignment needs no new buffer - and the slots past a.size() are
    // raw memory that must be constructed into, not assigned to.
    for (Case cs : {Case{2, 7, 0}, Case{2, 7, 16}, Case{7, 2, 0}, Case{4, 4, 0}, Case{0, 5, 0}, Case{5, 0, 0}}) {
        const std::string spare = cs.reserve ? " and capacity() >= 16" : "";  // std::string: see README gotchas
        INFO("a has " << cs.target << " element(s)" << spare << ", b has " << cs.source << "; then a = b");
        std::string problem;  // first problem of this case ("" if none); stop at the first broken case
        {
            vector<Tracked> a;
            vector<Tracked> b;
            if (cs.reserve) a.reserve(static_cast<std::size_t>(cs.reserve));
            push_values(a, 100, cs.target);
            push_values(b, 0, cs.source);
            REQUIRE(a.size() == static_cast<std::size_t>(cs.target));
            REQUIRE(b.size() == static_cast<std::size_t>(cs.source));
            vector<Tracked>& result = (a = b);
            const Verdict lifetime = expect::lifetime_clean();
            const Verdict live = expect::live(
                2 * cs.source,
                "after a = b both vectors hold b's elements; a's old elements must have been destroyed (or "
                "assigned over), and no extra objects may exist");
            if (&result != &a)
                problem = "operator= must return *this";
            else if (a.size() != static_cast<std::size_t>(cs.source))
                problem = Message() << "after a = b, a.size() is " << a.size() << ", expected b.size() == "
                                    << cs.source;
            else if (a.capacity() < a.size())
                problem = Message() << "after a = b, a.capacity() (" << a.capacity() << ") < a.size()";
            else if (const long bad = first_mismatch(a, 0, cs.source); bad != -1)
                problem = Message() << "after a = b, a[" << bad << "] is " << a[static_cast<std::size_t>(bad)]
                                    << ", expected " << bad;
            else if (first_mismatch(b, 0, cs.source) != -1)
                problem = "a = b changed b";
            else if (!lifetime.ok)
                problem = lifetime.message;
            else if (!live.ok) {
                problem = live.message;
                ::cppatlas::detail::ledger().reported_live = true;  // no second "still alive" at the end
            }
            else if (cs.source > 0 && &a[0] == &b[0])
                problem = "copy assignment must not share b's buffer";
        }
        if (problem.empty() && counts().live != 0) {
            const Verdict gone = expect::all_destroyed("after both vectors were destroyed");
            problem = gone.message;
        }
        if (!problem.empty()) {
            FAIL_CHECK(problem);
            break;
        }
        CHECK(true);
    }
}

CPPATLAS_TEST("copy self-assignment leaves the vector unchanged") {
    vector<Tracked> v;
    push_values(v, 0, 5);
    REQUIRE(v.size() == 5);
    const vector<Tracked>& alias = v;
    v = alias;
    REQUIRE_MESSAGE(v.size() == 5, "after v = v, size() is " << v.size()
                                                            << "\n-> if this == &other, return *this (or copy into a "
                                                               "new buffer before releasing the old one).");
    CHECK(first_mismatch(v, 0, 5) == -1);
    VERIFY(expect::live(5, "v = v must not create or lose elements"));
}

// ------------------------------------------------------------ move constructor

CPPATLAS_TEST("the move constructor steals the buffer: no element operations, same addresses") {
    vector<Tracked> v;
    push_values(v, 0, 8);
    REQUIRE(v.size() == 8);
    const Tracked* first = &v[0];
    const std::size_t cap = v.capacity();
    CounterScope scope;
    vector<Tracked> m(std::move(v));
    VERIFY(expect::no_element_ops(scope.delta(), "moving a vector (move constructor)",
                                  "the move constructor takes over other's pointer, size and capacity, then sets "
                                  "other's to nullptr/0. It must not copy or move individual elements - a plain "
                                  "`vector(vector&&)` that is missing falls back to the copy constructor."));
    REQUIRE(m.size() == 8);
    CHECK_MESSAGE((&m[0] == first), "the moved-to vector must own the very same buffer (&m[0] == old &v[0])");
    CHECK(m.capacity() == cap);
    CHECK(first_mismatch(m, 0, 8) == -1);
    CHECK_MESSAGE(v.size() == 0, "after vector m(std::move(v)), v.size() is "
                                     << v.size()
                                     << "\n-> leave the source empty (size 0, no buffer): its elements now belong to m.");
    VERIFY(expect::live(8, "the elements must exist exactly once, in the new vector"));
}

CPPATLAS_TEST("the move constructor is noexcept") {
    INFO("declare `vector(vector&& other) noexcept`: it only swaps pointers, and containers of vectors "
         "(including a vector<vector<T>>) rely on it to relocate by move instead of by copy");
    CHECK(std::is_nothrow_move_constructible_v<vector<Tracked>>);
    CHECK(std::is_nothrow_move_constructible_v<vector<MoveOnly>>);
}

CPPATLAS_TEST("a moved-from vector can be reused and destroyed") {
    vector<Tracked> v;
    push_values(v, 0, 3);
    vector<Tracked> m(std::move(v));
    REQUIRE(v.size() == 0);
    v.push_back(Tracked(42));
    REQUIRE(v.size() == 1);
    CHECK(v[0] == 42);
    v = m;  // copy-assign into it
    REQUIRE(v.size() == 3);
    CHECK(first_mismatch(v, 0, 3) == -1);
    vector<Tracked> other(std::move(m));
    m = vector<Tracked>();  // move-assign into a moved-from vector
    CHECK(m.size() == 0);
    REQUIRE(other.size() == 3);
}

// ------------------------------------------------------------ move assignment

CPPATLAS_TEST("move assignment moves no elements and destroys the target's old elements") {
    vector<Tracked> dst;
    vector<Tracked> src;
    push_values(dst, 100, 3);
    push_values(src, 0, 5);
    REQUIRE(dst.size() == 3);
    REQUIRE(src.size() == 5);
    const Tracked* first = &src[0];
    CounterScope scope;
    vector<Tracked>& result = (dst = std::move(src));
    const Counts d = scope.delta();
    CHECK_MESSAGE((&result == &dst), "operator= must return *this");
    CHECK_MESSAGE((d.constructions() == 0 && d.assignments() == 0),
                  "move assignment copied or moved elements: " << to_string(d)
                                                               << "\n-> take over other's buffer (pointer, size, "
                                                                  "capacity) instead of transferring elements.");
    VERIFY(expect::ops(d, Op::destroy, 3,
                       "the target's 3 old elements must be destroyed by the assignment (and its old buffer freed). "
                       "Swapping them into the source keeps them alive for an unknown time."));
    REQUIRE(dst.size() == 5);
    CHECK_MESSAGE((&dst[0] == first), "dst must now own src's old buffer (&dst[0] == old &src[0])");
    CHECK(first_mismatch(dst, 0, 5) == -1);
    // src is valid but unspecified: it may be used, never relied upon.
    const std::size_t left = src.size();
    VERIFY(expect::live(5 + static_cast<long>(left),
                        "every element object must belong to exactly one of the two vectors"));
    src.push_back(Tracked(9));
    REQUIRE(src.size() == left + 1);
    CHECK(src[left] == 9);
}

CPPATLAS_TEST("move assignment into an empty vector, and from an empty vector") {
    vector<Tracked> a;
    vector<Tracked> b;
    push_values(b, 0, 4);
    CounterScope scope;
    a = std::move(b);
    VERIFY(expect::no_element_ops(scope.delta(), "move-assigning into an empty vector"));
    REQUIRE(a.size() == 4);
    CHECK(first_mismatch(a, 0, 4) == -1);

    vector<Tracked> empty;
    scope.restart();
    a = std::move(empty);
    const Counts d = scope.delta();
    CHECK(d.constructions() == 0);
    CHECK_MESSAGE(d.dtor == 4, "a = std::move(empty) must destroy a's 4 old elements, saw " << to_string(d));
    CHECK(a.size() == 0);
    VERIFY(expect::live(static_cast<long>(b.size() + empty.size())));
}

CPPATLAS_TEST("move assignment is noexcept") {
    INFO("declare `vector& operator=(vector&& other) noexcept` (std::vector's is noexcept with std::allocator)");
    CHECK(std::is_nothrow_move_assignable_v<vector<Tracked>>);
}

CPPATLAS_TEST("a vector of a move-only type can be moved (construction and assignment)") {
    vector<MoveOnly> a;
    push_values(a, 0, 5);
    REQUIRE(a.size() == 5);
    CounterScope scope;
    vector<MoveOnly> b(std::move(a));
    vector<MoveOnly> c;
    c = std::move(b);
    VERIFY(expect::no_element_ops(scope.delta(), "moving a vector<MoveOnly>"));
    REQUIRE(c.size() == 5);
    CHECK(first_mismatch(c, 0, 5) == -1);
}
