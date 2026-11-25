// Stage 8 - insert-erase
// Spec: https://cppatlas.com/build/vector/insert-erase
//
// insert(const_iterator, const T&) / insert(const_iterator, T&&) and
// erase(const_iterator) / erase(const_iterator, const_iterator).
//
// Standard facts used ([vector.modifiers], [sequence.reqmts]):
//   - insert returns an iterator to the inserted element; it reallocates iff
//     the new size exceeds capacity(); without reallocation the storage stays
//     the same (iterators before the insertion point remain valid).
//   - erase returns an iterator to the element that followed the erased range
//     (end() if none); it invalidates only iterators at or after the erase
//     point, so it never reallocates. "The destructor of T is called the number
//     of times equal to the number of the elements erased, but the assignment
//     operator of T is called the number of times equal to the number of
//     elements in the vector after the erased elements." erase requires only
//     Cpp17MoveAssignable, and every implementation move-assigns (no copies).
//   - Exact copy/move counts of insert are NOT specified and differ between
//     implementations (libstdc++ copies an lvalue into a temporary and then
//     move-assigns it; libc++ copy-assigns it directly). The tests therefore
//     bound them: an rvalue of a noexcept-movable T is never copied; an lvalue
//     is copied at most twice. The standard only asks for linear complexity, so
//     moves are bounded linearly: 3 * (elements shifted) + 2, plus size() when
//     reallocating. The factor 3 admits an insert built from emplace_back +
//     std::rotate (rotate may swap, and a swap is 3 moves); a shift that is
//     quadratic, or that copies, still fails.
//   - insert(pos, x) where x aliases an element of the vector must work
//     (both libraries handle it; it follows from the value semantics of the
//     const T& parameter).
#include "support/harness.hpp"

#include <string>
#include <vector>  // std::vector<int> is used as a reference MODEL in one test

using namespace cppatlas;

namespace preflight {

CPPATLAS_PREFLIGHT(stage1_api,
                   "stage 8 (insert-erase) builds on stage 1: my::vector<T> needs `vector()`, "
                   "`size() const`, `capacity() const` and `empty() const` (run `./cppatlas test 1`).",
                   my::vector<Tracked>,
                   std::default_initializable<V> && requires(const V& v) {
                       { v.size() } -> std::convertible_to<std::size_t>;
                       { v.capacity() } -> std::convertible_to<std::size_t>;
                       { v.empty() } -> std::convertible_to<bool>;
                   });

CPPATLAS_PREFLIGHT(earlier_api,
                   "stage 8 (insert-erase) builds on stages 2-5: my::vector<T> needs `push_back(const T&)`, "
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

CPPATLAS_PREFLIGHT(iterator_api,
                   "stage 8 (insert-erase) builds on stage 7: my::vector<T> needs `iterator`, `const_iterator`, "
                   "`begin()`, `end()`, `cbegin()`, `cend()` (run `./cppatlas test 7`).",
                   my::vector<Tracked>,
                   requires(V& v) {
                       typename V::iterator;
                       typename V::const_iterator;
                       { v.begin() } -> std::same_as<typename V::iterator>;
                       { v.end() } -> std::same_as<typename V::iterator>;
                       { v.cbegin() } -> std::same_as<typename V::const_iterator>;
                       { v.cend() } -> std::same_as<typename V::const_iterator>;
                   });

CPPATLAS_PREFLIGHT(has_insert_copy,
                   "stage 8 (insert-erase): my::vector<T> needs `iterator insert(const_iterator pos, const T& value)`.",
                   my::vector<Tracked>,
                   requires(V& v, const Tracked& t) {
                       { v.insert(v.cbegin(), t) } -> std::same_as<typename V::iterator>;
                   });

// An rvalue also binds to insert(const_iterator, const T&): ask for the
// T&& overload by its exact signature.
CPPATLAS_PREFLIGHT(has_insert_move,
                   "stage 8 (insert-erase): my::vector<T> needs `iterator insert(const_iterator pos, T&& value)`.",
                   my::vector<Tracked>,
                   requires {
                       static_cast<typename V::iterator (V::*)(typename V::const_iterator, Tracked&&)>(&V::insert);
                   });

CPPATLAS_PREFLIGHT(has_erase_one,
                   "stage 8 (insert-erase): my::vector<T> needs `iterator erase(const_iterator pos)`.",
                   my::vector<Tracked>,
                   requires(V& v) {
                       { v.erase(v.cbegin()) } -> std::same_as<typename V::iterator>;
                   });

CPPATLAS_PREFLIGHT(has_erase_range,
                   "stage 8 (insert-erase): my::vector<T> needs "
                   "`iterator erase(const_iterator first, const_iterator last)`.",
                   my::vector<Tracked>,
                   requires(V& v) {
                       { v.erase(v.cbegin(), v.cend()) } -> std::same_as<typename V::iterator>;
                   });

}  // namespace preflight

CPPATLAS_STAGE_GATE(preflight::stage1_api && preflight::earlier_api && preflight::iterator_api &&
                    preflight::has_insert_copy && preflight::has_insert_move && preflight::has_erase_one &&
                    preflight::has_erase_range);

namespace {

// A vector holding 0..n-1. spare == true: capacity() >= n + 8 (insert does not
// reallocate). spare == false: size() == capacity() (insert must reallocate);
// then the size is the smallest full size >= n, returned.
template <class V>
std::size_t build(V& v, int n, bool spare) {
    if (spare) v.reserve(static_cast<std::size_t>(n) + 8);
    for (int i = 0; i < 4096; ++i) {
        const bool done = spare ? v.size() == static_cast<std::size_t>(n)
                                : (v.size() >= static_cast<std::size_t>(n) && v.size() == v.capacity());
        if (done) return v.size();
        v.emplace_back(static_cast<int>(v.size()));
    }
    FAIL("could not build the test vector (size() " << v.size() << ", capacity() " << v.capacity() << ")");
    return 0;
}

// Checks v == 0..n-1 with `value` inserted at index `at`. Returns "" or a description.
template <class V>
std::string check_inserted(const V& v, std::size_t n, std::size_t at, int value) {
    if (v.size() != n + 1)
        return (Message() << "size() is " << v.size() << ", expected " << n + 1).str();
    for (std::size_t i = 0; i < n + 1; ++i) {
        const int want = i < at ? static_cast<int>(i) : i == at ? value : static_cast<int>(i) - 1;
        if (!(v[i] == want))
            return (Message() << "v[" << i << "] is " << v[i] << ", expected " << want
                              << " (the elements before the insertion point stay, the ones from it on shift right by "
                                 "one, in order)")
                .str();
    }
    return {};
}

// Checks v == 0..n-1 with indices [first, last) removed.
template <class V>
std::string check_erased(const V& v, std::size_t n, std::size_t first, std::size_t last) {
    const std::size_t expect_size = n - (last - first);
    if (v.size() != expect_size)
        return (Message() << "size() is " << v.size() << ", expected " << expect_size).str();
    for (std::size_t i = 0; i < expect_size; ++i) {
        const int want = static_cast<int>(i < first ? i : i + (last - first));
        if (!(v[i] == want))
            return (Message() << "v[" << i << "] is " << v[i] << ", expected " << want
                              << " (the remaining elements must keep their order)")
                .str();
    }
    return {};
}

// Upper bound on the moves an insert may spend shifting `shifted` elements by
// one slot: linear, with room for a rotate-based insert (3 moves per swap).
long max_shift_moves(std::size_t shifted) { return 3 * static_cast<long>(shifted) + 2; }

// std::string, not const char*: doctest prints a non-literal `const char*` as a bool.
std::string where_name(std::size_t at, std::size_t n) { return at == 0 ? "begin" : at == n ? "end" : "middle"; }

}  // namespace

// ---------------------------------------------------------------- insert

CPPATLAS_TEST("insert(pos, T&&) at begin / middle / end, without reallocation") {
    for (int where = 0; where < 3; ++where) {
        vector<Tracked> v;
        const std::size_t n = build(v, 6, true);
        REQUIRE(n == 6);
        const std::size_t at = where == 0 ? 0 : where == 1 ? 3 : n;
        const std::string place = where_name(at, n);
        CAPTURE(place);
        const std::size_t cap = v.capacity();
        const Tracked* first = &v[0];
        Tracked x(99);
        CounterScope scope;
        auto it = v.insert(v.cbegin() + static_cast<std::ptrdiff_t>(at), std::move(x));
        const Counts d = scope.delta();
        const std::string bad = check_inserted(v, n, at, 99);
        REQUIRE_MESSAGE(bad.empty(), "after insert at " << where_name(at, n) << ": " << bad);
        CHECK_MESSAGE(it - v.begin() == static_cast<std::ptrdiff_t>(at),
                      "insert must return an iterator to the inserted element (index " << at << "), got index "
                                                                                       << (it - v.begin()));
        CHECK_MESSAGE(v.capacity() == cap, "insert reallocated although capacity() (" << cap << ") > size()");
        CHECK_MESSAGE((&v[0] == first), "insert moved the elements to a new buffer although there was spare capacity");
        VERIFY(expect::no_copies(d, "insert(pos, T&&) of a noexcept-movable T",
                                 "shift the tail with moves (move-construct the last element into the free slot, "
                                 "std::move_backward the rest) and move the value in with std::move(value)."));
        VERIFY(expect::at_most(d.moves(), max_shift_moves(n - at), "moves during insert",
                               "only the elements at or after pos need to shift, each by one slot (a handful of "
                               "moves per shifted element at most)."));
        CHECK_MESSAGE(d.moves() >= 1, "the value must be moved into the vector: " << to_string(d));
        VERIFY(expect::live(static_cast<long>(n) + 2, "n + 1 elements in the vector plus the moved-from argument"));
    }
}

CPPATLAS_TEST("insert(pos, const T&) at begin / middle / end, without reallocation") {
    for (int where = 0; where < 3; ++where) {
        vector<Tracked> v;
        const std::size_t n = build(v, 6, true);
        REQUIRE(n == 6);
        const std::size_t at = where == 0 ? 0 : where == 1 ? 2 : n;
        const std::string place = where_name(at, n);
        CAPTURE(place);
        const std::size_t cap = v.capacity();
        const Tracked x(77);
        CounterScope scope;
        auto it = v.insert(v.begin() + static_cast<std::ptrdiff_t>(at), x);  // iterator -> const_iterator
        const Counts d = scope.delta();
        const std::string bad = check_inserted(v, n, at, 77);
        REQUIRE_MESSAGE(bad.empty(), "after insert at " << where_name(at, n) << ": " << bad);
        CHECK(it - v.begin() == static_cast<std::ptrdiff_t>(at));
        CHECK(*it == 77);
        CHECK(x == 77);
        CHECK(v.capacity() == cap);
        CHECK_MESSAGE(d.copies() >= 1, "the value must be copied into the vector: " << to_string(d));
        VERIFY(expect::at_most(d.copies(), 2, "copies during insert(pos, const T&)",
                               "copy only the new value; the existing elements are shifted by moving them (their "
                               "move constructor is noexcept)."));
        VERIFY(expect::at_most(d.moves(), max_shift_moves(n - at), "moves during insert",
                               "only the elements at or after pos need to shift, each by one slot."));
        VERIFY(expect::live(static_cast<long>(n) + 2));
    }
}

CPPATLAS_TEST("insert at begin / middle / end with reallocation (rvalue and lvalue)") {
    for (int lvalue = 0; lvalue < 2; ++lvalue) {
        for (int where = 0; where < 3; ++where) {
            vector<Tracked> v;
            const std::size_t n = build(v, 5, false);
            REQUIRE(n >= 5);
            REQUIRE(v.size() == v.capacity());
            const std::size_t at = where == 0 ? 0 : where == 1 ? n / 2 : n;
            const std::string place = where_name(at, n);
            CAPTURE(place);
            CAPTURE(lvalue);
            const std::size_t cap = v.capacity();
            Tracked x(55);
            CounterScope scope;
            auto it = lvalue ? v.insert(v.cbegin() + static_cast<std::ptrdiff_t>(at), x)
                             : v.insert(v.cbegin() + static_cast<std::ptrdiff_t>(at), std::move(x));
            const Counts d = scope.delta();
            const std::string bad = check_inserted(v, n, at, 55);
            REQUIRE_MESSAGE(bad.empty(), "after insert at " << where_name(at, n) << " into a full vector: " << bad);
            CHECK_MESSAGE(it - v.begin() == static_cast<std::ptrdiff_t>(at),
                          "insert must return an iterator into the NEW buffer, at the inserted element");
            CHECK(*it == 55);
            CHECK(v.capacity() > cap);
            VERIFY(expect::at_most(d.copies(), lvalue ? 1 : 0, "copies during insert with reallocation",
                                   "relocate the existing elements by moving them (std::move_if_noexcept) and "
                                   "construct the new value directly in the new buffer."));
            VERIFY(expect::at_most(d.moves(), static_cast<long>(n) + max_shift_moves(n - at),
                                   "moves during insert with reallocation",
                                   "every old element is relocated once, and at most the elements from pos on are "
                                   "shifted; nothing else needs to move."));
            VERIFY(expect::live(static_cast<long>(n) + 2,
                                "n + 1 elements plus the argument: the old buffer's elements must be destroyed"));
        }
    }
}

CPPATLAS_TEST("insert with an argument that aliases an element: v.insert(v.begin(), v[size() - 1]) and v[1]") {
    for (int spare = 0; spare < 2; ++spare) {
        CAPTURE(spare);
        {
            vector<Tracked> v;
            const std::size_t n = build(v, 5, spare != 0);
            REQUIRE(n >= 5);
            const int last = static_cast<int>(n) - 1;
            v.insert(v.begin(), v[v.size() - 1]);  // the last element
            REQUIRE(v.size() == n + 1);
            CHECK_MESSAGE(v[0] == last, "v.insert(v.begin(), v[v.size() - 1]) put "
                                            << v[0] << " at the front, expected " << last
                                            << "\n-> the argument refers INTO the vector: after shifting, that "
                                               "slot holds a different element (or, after a reallocation, it is "
                                               "freed memory). Copy the value first (or construct it in the new "
                                               "buffer before relocating).");
            bool rest = true;
            for (std::size_t i = 1; i < n + 1; ++i) rest = rest && (v[i] == static_cast<int>(i) - 1);
            CHECK_MESSAGE(rest, "the original elements must follow, in order");
        }
        {
            vector<Tracked> v;
            const std::size_t n = build(v, 5, spare != 0);
            REQUIRE(n >= 5);
            v.insert(v.begin(), v[1]);
            REQUIRE(v.size() == n + 1);
            CHECK_MESSAGE(v[0] == 1, "v.insert(v.begin(), v[1]) put " << v[0] << " at the front, expected 1"
                                                                      << "\n-> v[1] moves to v[2] during the shift: "
                                                                         "copy the argument before shifting.");
            CHECK(v[1] == 0);
            CHECK(v[2] == 1);
        }
    }
}

CPPATLAS_TEST("insert with an aliasing argument, std::string elements (heap-owning)") {
    for (int spare = 0; spare < 2; ++spare) {
        CAPTURE(spare);
        vector<std::string> v;
        if (spare) v.reserve(16);
        const std::string tail(40, 'z');
        v.push_back(std::string(40, 'a'));
        v.push_back(std::string(40, 'b'));
        v.push_back(tail);
        while (!spare && v.size() != v.capacity() && v.size() < 4096) v.push_back(tail);
        const std::size_t n = v.size();
        v.insert(v.begin(), v[n - 1]);
        REQUIRE(v.size() == n + 1);
        CHECK(v[0] == tail);
        CHECK(v[1] == std::string(40, 'a'));
        CHECK(v[n] == tail);
    }
}

CPPATLAS_TEST("repeated insertion at the front keeps every element (with reallocations)") {
    vector<Tracked> v;
    for (int i = 0; i < 100; ++i) {
        auto it = v.insert(v.begin(), Tracked(i));
        if (v.size() != static_cast<std::size_t>(i) + 1 || it != v.begin()) {
            FAIL_CHECK("insert #" << i + 1 << " at begin(): size() " << v.size() << ", returned iterator at index "
                                  << (it - v.begin()));
            break;
        }
    }
    REQUIRE(v.size() == 100);
    bool reversed = true;
    for (int i = 0; i < 100; ++i) reversed = reversed && (v[static_cast<std::size_t>(i)] == 99 - i);
    CHECK_MESSAGE(reversed, "inserting 0..99 at the front must give 99..0");
    VERIFY(expect::live(100));
}

CPPATLAS_TEST("insert(pos, T&&) works with a move-only type") {
    vector<MoveOnly> v;
    for (int i = 0; i < 20; ++i) {
        const std::size_t at = v.size() / 2;
        v.insert(v.cbegin() + static_cast<std::ptrdiff_t>(at), MoveOnly(i));
        REQUIRE(v.size() == static_cast<std::size_t>(i) + 1);
        REQUIRE(v[at] == i);
    }
    v.erase(v.cbegin());
    v.erase(v.cbegin() + 3, v.cbegin() + 6);
    REQUIRE(v.size() == 16);
    VERIFY(expect::live(16));
}

// ----------------------------------------------------------------- erase

// erase never reallocates. Reported once, ahead of the element counts (which
// would otherwise each fail for the same reason).
template <class V>
Verdict erase_kept_buffer(const Snapshot& before, const V& v, std::string_view what) {
    if (v.capacity() == before.capacity && (v.size() == 0 || static_cast<const void*>(&v[0]) == before.first))
        return Verdict::pass();
    Message m;
    if (v.capacity() != before.capacity)
        m << what << " changed capacity() from " << before.capacity << " to " << v.capacity() << ".";
    else
        m << what << " moved the elements to a new buffer (&v[0] was " << before.first << ", is now "
          << static_cast<const void*>(&v[0]) << ").";
    m << "\n-> erase never reallocates: shift the elements after the erased range left IN PLACE by "
         "move-assignment (std::move(last, end(), first)), then destroy the now-unused slots at the end.";
    return Verdict::fail(m);
}

CPPATLAS_TEST("erase(pos): one destruction, (elements after) move-assignments, no copies, no reallocation") {
    for (std::size_t at : {std::size_t{0}, std::size_t{3}, std::size_t{7}}) {
        CAPTURE(at);
        vector<Tracked> v;
        const std::size_t n = build(v, 8, true);
        REQUIRE(n == 8);
        const Snapshot before = snapshot(v);
        CounterScope scope;
        auto it = v.erase(v.cbegin() + static_cast<std::ptrdiff_t>(at));
        const Counts d = scope.delta();
        const std::string bad = check_erased(v, n, at, at + 1);
        REQUIRE_MESSAGE(bad.empty(), "after erase(begin() + " << at << "): " << bad);
        REQUIRE_VERIFY(erase_kept_buffer(before, v, "erase(pos)"));
        CHECK_MESSAGE(it - v.begin() == static_cast<std::ptrdiff_t>(at),
                      "erase must return an iterator to the element that followed the erased one (index "
                          << at << "), got index " << (it - v.begin()));
        if (at + 1 == n) CHECK_MESSAGE(it == v.end(), "erasing the last element must return end()");
        bool prefix_same = true;
        for (std::size_t i = 0; i < at; ++i) prefix_same = prefix_same && v[i].id() == before.ids[i];
        // One message per broken position, then stop (the other positions fail the same way).
        const Verdict counts_ok = expect::first_failure({
            expect::ops(d, Op::destroy, 1,
                        "erase destroys exactly one element: shift the tail left by move-ASSIGNMENT "
                        "(std::move(pos + 1, end, pos)), then destroy the now-unused last slot."),
            expect::ops(d, Op::move_assign, static_cast<long>(n - at - 1),
                        "each element after the erased one is move-assigned one slot to the left, exactly once."),
            expect::no_copies(d, "erase", "shift with std::move / move assignment, never copies."),
            d.constructions() == 0 ? Verdict::pass()
                                   : Verdict::fail(Message() << "erase constructed elements: " << to_string(d)
                                                             << "\n-> erase only assigns and destroys."),
            prefix_same ? Verdict::pass()
                        : Verdict::fail("elements before the erase point must not be touched (their ids changed)"),
        });
        VERIFY(counts_ok);
        VERIFY(expect::live(static_cast<long>(n) - 1));
        if (!counts_ok.ok) break;
    }
}

CPPATLAS_TEST("erase(first, last): destroys exactly last - first, move-assigns the elements after") {
    vector<Tracked> v;
    const std::size_t n = build(v, 10, true);
    REQUIRE(n == 10);
    const Snapshot before = snapshot(v);
    CounterScope scope;
    auto it = v.erase(v.cbegin() + 2, v.cbegin() + 5);
    const Counts d = scope.delta();
    const std::string bad = check_erased(v, n, 2, 5);
    REQUIRE_MESSAGE(bad.empty(), "after erase(begin() + 2, begin() + 5): " << bad);
    REQUIRE_VERIFY(erase_kept_buffer(before, v, "erase(first, last)"));
    CHECK_MESSAGE(it - v.begin() == 2, "erase(first, last) must return an iterator at the old position of `last`");
    CHECK(*it == 5);
    VERIFY(expect::first_failure({
        expect::ops(d, Op::destroy, 3, "erasing 3 elements destroys exactly 3."),
        expect::ops(d, Op::move_assign, 5, "the 5 elements after the range are each move-assigned once."),
        expect::no_copies(d, "erase(first, last)"),
        d.constructions() == 0 ? Verdict::pass()
                               : Verdict::fail(Message() << "erase constructed elements: " << to_string(d)
                                                         << "\n-> erase only assigns and destroys."),
    }));
    VERIFY(expect::live(7));
}

CPPATLAS_TEST("erase(first, first) of an empty range does nothing and returns first") {
    vector<Tracked> v;
    const std::size_t n = build(v, 5, true);
    REQUIRE(n == 5);
    const Snapshot before = snapshot(v);
    CounterScope scope;
    auto it = v.erase(v.cbegin() + 2, v.cbegin() + 2);
    VERIFY(expect::no_element_ops(scope.delta(), "erase of an empty range"));
    CHECK(it - v.begin() == 2);
    VERIFY(expect::unchanged(before, v, "erase(first, first)"));
    scope.restart();
    auto e = v.erase(v.cend(), v.cend());
    VERIFY(expect::no_element_ops(scope.delta(), "erase(end(), end())"));
    CHECK(e == v.end());
}

CPPATLAS_TEST("erase up to end(), and erase everything") {
    {
        vector<Tracked> v;
        const std::size_t n = build(v, 8, true);
        REQUIRE(n == 8);
        CounterScope scope;
        auto it = v.erase(v.cbegin() + 3, v.cend());
        const Counts d = scope.delta();
        REQUIRE(check_erased(v, n, 3, n).empty());
        CHECK_MESSAGE(it == v.end(), "erasing a suffix must return the new end()");
        VERIFY(expect::ops(d, Op::destroy, 5));
        CHECK_MESSAGE(d.assignments() == 0, "erasing a suffix needs no assignments: " << to_string(d));
    }
    {
        vector<Tracked> v;
        const std::size_t n = build(v, 8, false);
        const std::size_t cap = v.capacity();
        CounterScope scope;
        auto it = v.erase(v.cbegin(), v.cend());
        const Counts d = scope.delta();
        CHECK(v.size() == 0);
        CHECK(v.empty());
        CHECK(it == v.end());
        CHECK(it == v.begin());
        CHECK(v.capacity() == cap);
        VERIFY(expect::ops(d, Op::destroy, static_cast<long>(n)));
        CHECK(d.assignments() == 0);
        VERIFY(expect::live(0));
        v.push_back(Tracked(1));  // still usable
        CHECK(v.size() == 1);
    }
}

CPPATLAS_TEST("a long mixed sequence of insert / erase matches a reference model, without leaks") {
    vector<Tracked> v;
    std::vector<int> model;  // harness-side reference, not the subject
    unsigned state = 12345u;
    auto rnd = [&state](unsigned bound) {
        state = state * 1103515245u + 12345u;
        return bound == 0 ? 0u : (state >> 16) % bound;
    };
    for (int step = 0; step < 400; ++step) {
        const unsigned op = rnd(4);
        const std::size_t sz = model.size();
        if (op <= 1 || sz == 0) {  // insert (rvalue or lvalue)
            const std::size_t at = rnd(static_cast<unsigned>(sz) + 1);
            if (op == 0) {
                v.insert(v.cbegin() + static_cast<std::ptrdiff_t>(at), Tracked(step));
            } else {
                const Tracked x(step);
                v.insert(v.cbegin() + static_cast<std::ptrdiff_t>(at), x);
            }
            model.insert(model.begin() + static_cast<std::ptrdiff_t>(at), step);
        } else if (op == 2) {  // erase one
            const std::size_t at = rnd(static_cast<unsigned>(sz));
            v.erase(v.cbegin() + static_cast<std::ptrdiff_t>(at));
            model.erase(model.begin() + static_cast<std::ptrdiff_t>(at));
        } else {  // erase a range (possibly empty)
            const std::size_t a = rnd(static_cast<unsigned>(sz) + 1);
            const std::size_t b = a + rnd(static_cast<unsigned>(sz - a) + 1);
            v.erase(v.cbegin() + static_cast<std::ptrdiff_t>(a), v.cbegin() + static_cast<std::ptrdiff_t>(b));
            model.erase(model.begin() + static_cast<std::ptrdiff_t>(a), model.begin() + static_cast<std::ptrdiff_t>(b));
        }
        REQUIRE_MESSAGE(v.size() == model.size(), "after step " << step << " size() is " << v.size() << ", expected "
                                                                << model.size());
        bool same = true;
        for (std::size_t i = 0; i < model.size() && same; ++i) same = v[i] == model[i];
        REQUIRE_MESSAGE(same, "after step " << step << " the contents differ from the expected sequence");
        REQUIRE_MESSAGE(counts().live == static_cast<long>(model.size()),
                        "after step " << step << " " << counts().live << " objects are alive for " << model.size()
                                      << " elements");
    }
}
