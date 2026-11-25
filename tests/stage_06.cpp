// Stage 6 - exception-safety
// Spec: https://cppatlas.com/build/vector/exception-safety
//
// Strong exception guarantee for reallocation (push_back / emplace_back /
// reserve), and relocation by std::move_if_noexcept.
//
// Standard facts used:
//   - [vector.modifiers] push_back / emplace_back: "If an exception is thrown
//     while inserting a single element at the end and T is Cpp17CopyInsertable
//     or is_nothrow_move_constructible_v<T> is true, there are no effects."
//     That covers a throw from the NEW element's constructor as well as from a
//     copy made while relocating, with or without reallocation.
//   - [vector.capacity] reserve: "If an exception is thrown other than by the
//     move constructor of a non-Cpp17CopyInsertable type, there are no effects."
//   - For a move-only T whose move may throw, only the basic guarantee holds:
//     the vector stays valid and nothing leaks ([res.on.exception.handling]).
//   - Which copy comes first (the new element, or the relocated ones) is NOT
//     specified, so the tests inject a throw at EVERY position, 1..size()+1.
//   - "No effects" is checked as: same size(), capacity(), &v[0], and the very
//     same element objects (ids) with the same values; no leaked objects.
#include "support/harness.hpp"

#include <string>

using namespace cppatlas;

namespace preflight {

CPPATLAS_PREFLIGHT(stage1_api,
                   "stage 6 (exception-safety) builds on stage 1: my::vector<T> needs `vector()`, "
                   "`size() const`, `capacity() const` and `empty() const` (run `./cppatlas test 1`).",
                   my::vector<Tracked>,
                   std::default_initializable<V> && requires(const V& v) {
                       { v.size() } -> std::convertible_to<std::size_t>;
                       { v.capacity() } -> std::convertible_to<std::size_t>;
                       { v.empty() } -> std::convertible_to<bool>;
                   });

CPPATLAS_PREFLIGHT(stage2_api,
                   "stage 6 (exception-safety) builds on stage 2: my::vector<T> needs `void push_back(const T&)`, "
                   "`T& operator[](std::size_t)` and `const T& operator[](std::size_t) const`.",
                   my::vector<Tracked>,
                   requires(V& v, const V& cv, const Tracked& t, std::size_t i) {
                       v.push_back(t);
                       { v[i] } -> std::same_as<Tracked&>;
                       { cv[i] } -> std::same_as<const Tracked&>;
                   });

CPPATLAS_PREFLIGHT(has_reserve,
                   "stage 6 (exception-safety) builds on stage 3: my::vector<T> needs `void reserve(std::size_t)`.",
                   my::vector<Tracked>,
                   requires(V& v, std::size_t n) { v.reserve(n); });

CPPATLAS_PREFLIGHT(has_push_back_rvalue,
                   "stage 6 (exception-safety) builds on stage 4: my::vector<T> needs `void push_back(T&& value)`.",
                   my::vector<Tracked>,
                   requires { static_cast<void (V::*)(Tracked&&)>(&V::push_back); });

CPPATLAS_PREFLIGHT(has_emplace_back,
                   "stage 6 (exception-safety) builds on stage 5: my::vector<T> needs "
                   "`template <class... Args> T& emplace_back(Args&&... args)`.",
                   my::vector<Tracked>,
                   requires(V& v) { v.emplace_back(1); });

}  // namespace preflight

CPPATLAS_STAGE_GATE(preflight::stage1_api && preflight::stage2_api && preflight::has_reserve &&
                    preflight::has_push_back_rvalue && preflight::has_emplace_back);

namespace {

// emplace_back(0), (1), ... until size() == capacity() and size() >= min_size
// (bounded). Returns the size reached.
template <class V>
std::size_t fill_to_capacity(V& v, std::size_t min_size) {
    for (int i = 0; i < 4096; ++i) {
        if (v.size() >= min_size && v.size() == v.capacity()) return v.size();
        v.emplace_back(static_cast<int>(v.size()));
    }
    FAIL("size() (" << v.size() << ") never reached capacity() (" << v.capacity() << ") within 4096 emplace_backs");
    return 0;
}

template <class V>
long first_mismatch(const V& v, int n) {
    for (int i = 0; i < n; ++i)
        if (!(v[static_cast<std::size_t>(i)] == i)) return i;
    return -1;
}

constexpr const char* kStrongHint =
    "build the new buffer completely BEFORE touching the old one: allocate, construct the new element and "
    "copy (std::move_if_noexcept) the old elements into it; if anything throws, destroy what was constructed in "
    "the new buffer, free it, and rethrow - the old buffer, size and capacity must still be untouched. Only after "
    "everything succeeded destroy the old elements, free the old buffer and switch the pointers.";

// For every k in 1..max_k: arm a throw on the k-th `op`, run `action(v)`, and
// require InjectedError + "no effects". Stops at the first broken k.
// Returns false (after reporting) if the vector misbehaved.
template <class V, class Action>
bool inject_each(V& v, Op op, long max_k, const char* what_c, Action action, const char* hint = kStrongHint) {
    // std::string: doctest prints a non-literal `const char*` as a bool.
    const std::string what = what_c;
    const std::string op_s = op_name(op);
    for (long k = 1; k <= max_k; ++k) {
        CAPTURE(k);
        const Snapshot before = snapshot(v);
        const long live_before = counts().live;
        bool threw = false;
        bool fired = false;
        {
            ThrowOn arm(op, k);
            try {
                action(v);
            } catch (const InjectedError&) {
                threw = true;
            }
            fired = arm.fired();
        }
        if (!fired) {
            // max_k > 1 only for a reallocation of a MayThrowMove vector: then the
            // missing operations are relocation copies.
            const bool relocating = before.size == before.capacity && max_k > 1;
            const std::string why =
                relocating ? "T's move constructor may throw, so relocation must COPY every element "
                             "(std::move_if_noexcept), and the new element is constructed from the argument "
                             "exactly once."
                           : "the new element must be constructed exactly once, directly from the argument(s) "
                             "(std::construct_at(slot, std::forward<Args>(args)...)), not default-constructed "
                             "and then assigned.";
            FAIL_CHECK(what << ": expected " << max_k << " x " << op_s << " (vector size " << before.size
                            << ", capacity " << before.capacity << "), but " << op_s << " #" << k
                            << " never happened (only " << k - 1 << " did).\n-> " << why);
            return false;
        }
        if (!threw) {
            FAIL_CHECK(what << ": the injected exception (on " << op_s << " #" << k
                            << ") did not reach the caller\n-> do not swallow exceptions: clean up and rethrow "
                               "(`catch (...) { ...; throw; }`).");
            return false;
        }
        const std::string label = (Message() << what << " that threw on " << op_s << " #" << k).str();
        const Verdict u = expect::unchanged(before, v, label, hint);
        if (!u.ok) {
            FAIL_CHECK(u.message);
            return false;
        }
        if (counts().live != live_before) {
            ::cppatlas::detail::ledger().reported_live = true;  // the end-of-test check would only repeat it
            FAIL_CHECK(what << " that threw on " << op_s << " #" << k << " changed the number of live objects "
                            << "from " << live_before << " to " << counts().live
                            << "\n-> when a construction in the new buffer throws, destroy the elements already "
                               "constructed there (and nothing else) before freeing it.");
            return false;
        }
        const Verdict lc = expect::lifetime_clean();
        if (!lc.ok) {
            FAIL_CHECK(lc.message);
            return false;
        }
    }
    return true;
}

}  // namespace

// --------------------------------------------------- strong guarantee: copies

CPPATLAS_TEST("push_back(const T&) at full capacity: a throwing copy (any of them) leaves the vector unchanged") {
    vector<MayThrowMove> v;
    const std::size_t n = fill_to_capacity(v, 4);
    REQUIRE(n >= 4);
    const MayThrowMove x(99);
    // n copies relocate the old elements, 1 copy creates the new one: inject on each.
    if (!inject_each(v, Op::copy_construct, static_cast<long>(n) + 1, "push_back(const T&) with reallocation",
                [&](auto& vec) { vec.push_back(x); })) return;
    // Still usable afterwards.
    v.push_back(x);
    REQUIRE(v.size() == n + 1);
    CHECK(first_mismatch(v, static_cast<int>(n)) == -1);
    CHECK(v[n] == 99);
}

CPPATLAS_TEST("emplace_back at full capacity: a throwing relocation copy leaves the vector unchanged") {
    vector<MayThrowMove> v;
    const std::size_t n = fill_to_capacity(v, 4);
    REQUIRE(n >= 4);
    if (!inject_each(v, Op::copy_construct, static_cast<long>(n), "emplace_back with reallocation",
                [](auto& vec) { vec.emplace_back(99); })) return;
    v.emplace_back(99);
    REQUIRE(v.size() == n + 1);
    CHECK(first_mismatch(v, static_cast<int>(n)) == -1);
}

CPPATLAS_TEST("emplace_back / push_back(T&&) at full capacity: a throwing NEW element leaves the vector unchanged") {
    vector<MayThrowMove> v;
    const std::size_t n = fill_to_capacity(v, 4);
    REQUIRE(n >= 4);
    if (!inject_each(v, Op::value_construct, 1, "emplace_back(99) whose constructor throws, with reallocation",
                [](auto& vec) { vec.emplace_back(99); })) return;
    MayThrowMove x(98);
    if (!inject_each(v, Op::move_construct, 1, "push_back(T&&) whose move constructor throws, with reallocation",
                [&](auto& vec) { vec.push_back(std::move(x)); })) return;
    v.emplace_back(99);
    REQUIRE(v.size() == n + 1);
    CHECK(first_mismatch(v, static_cast<int>(n)) == -1);
}

CPPATLAS_TEST("emplace_back / push_back(const T&) at full capacity, noexcept-move T: a throwing NEW element leaves the vector unchanged") {
    // [vector.modifiers]: T is Cpp17CopyInsertable and nothrow-move-constructible,
    // so a throw from the new element's constructor must have no effects. The
    // old elements are relocated by MOVE (noexcept), and moves cannot be undone
    // if the new element's constructor throws afterwards.
    constexpr const char* kNewFirst =
        "Tracked's move is noexcept, so relocation MOVES the old elements, leaving the originals moved-from. "
        "Construct the NEW element in the new buffer first and relocate the old elements only after it "
        "succeeded: if its constructor throws then, the old buffer is still untouched.";
    vector<Tracked> v;
    const std::size_t n = fill_to_capacity(v, 4);
    REQUIRE(n >= 4);
    if (!inject_each(v, Op::value_construct, 1, "emplace_back(99) whose constructor throws, with reallocation (Tracked)",
                     [](auto& vec) { vec.emplace_back(99); }, kNewFirst)) return;
    const Tracked x(98);
    if (!inject_each(v, Op::copy_construct, 1, "push_back(const T&) whose copy throws, with reallocation (Tracked)",
                     [&](auto& vec) { vec.push_back(x); }, kNewFirst)) return;
    v.push_back(x);
    REQUIRE(v.size() == n + 1);
    CHECK(first_mismatch(v, static_cast<int>(n)) == -1);
    CHECK(v[n] == 98);
}

CPPATLAS_TEST("reserve: a throwing copy (any of them) leaves the vector unchanged") {
    vector<MayThrowMove> v;
    const std::size_t n = fill_to_capacity(v, 5);
    REQUIRE(n >= 5);
    const std::size_t want = 2 * n + 10;
    if (!inject_each(v, Op::copy_construct, static_cast<long>(n), "reserve() with reallocation",
                [&](auto& vec) { vec.reserve(want); })) return;
    v.reserve(want);
    CHECK(v.capacity() >= want);
    REQUIRE(v.size() == n);
    CHECK(first_mismatch(v, static_cast<int>(n)) == -1);
}

CPPATLAS_TEST("without reallocation, a throwing new element leaves the vector unchanged") {
    vector<MayThrowMove> v;
    v.reserve(16);
    v.emplace_back(0);
    v.emplace_back(1);
    REQUIRE(v.size() == 2);
    REQUIRE(v.capacity() > 2);
    const MayThrowMove x(5);
    if (!inject_each(v, Op::copy_construct, 1, "push_back(const T&) with spare capacity",
                [&](auto& vec) { vec.push_back(x); })) return;
    if (!inject_each(v, Op::value_construct, 1, "emplace_back with spare capacity",
                [](auto& vec) { vec.emplace_back(5); })) return;
    vector<Tracked> t;  // nothrow-move type, throwing copy
    t.reserve(4);
    t.emplace_back(0);
    const Tracked y(1);
    if (!inject_each(t, Op::copy_construct, 1, "push_back(const T&) of a Tracked with spare capacity",
                [&](auto& vec) { vec.push_back(y); })) return;
    CHECK(t.size() == 1);
}

// --------------------------------------------------------- move_if_noexcept

CPPATLAS_TEST("relocation moves a type whose move constructor is noexcept (0 copies)") {
    vector<Tracked> v;
    for (int i = 0; i < 6; ++i) v.push_back(Tracked(i));
    REQUIRE(v.size() == 6);
    CounterScope scope;
    v.reserve(v.capacity() + 20);
    VERIFY(expect::relocated_by_move(scope.delta(), 6));
    REQUIRE(v.size() == 6);
    CHECK(first_mismatch(v, 6) == -1);

    // The same through push_back at full capacity.
    const std::size_t n = fill_to_capacity(v, 7);
    REQUIRE(n >= 7);
    scope.restart();
    v.push_back(Tracked(static_cast<int>(n)));
    const Counts d = scope.delta();
    VERIFY(expect::no_copies(d, "push_back(T&&) of a noexcept-movable T with reallocation",
                             "relocate with std::move_if_noexcept(old[i]): for a noexcept move it moves."));
    REQUIRE(v.size() == n + 1);
    CHECK(first_mismatch(v, static_cast<int>(n) + 1) == -1);
}

CPPATLAS_TEST("relocation copies a copyable type whose move constructor may throw (0 moves)") {
    vector<MayThrowMove> v;
    for (int i = 0; i < 6; ++i) v.emplace_back(i);
    REQUIRE(v.size() == 6);
    CounterScope scope;
    v.reserve(v.capacity() + 20);
    VERIFY(expect::relocated_by_copy(scope.delta(), 6));
    REQUIRE(v.size() == 6);
    CHECK(first_mismatch(v, 6) == -1);

    vector<CopyOnly> c;  // no move constructor at all: copied, obviously
    for (int i = 0; i < 4; ++i) c.emplace_back(i);
    scope.restart();
    c.reserve(c.capacity() + 20);
    VERIFY(expect::relocated_by_copy(scope.delta(), 4));
}

CPPATLAS_TEST("push_back(T&&) at full capacity: the new element is moved in, the old ones are copied") {
    vector<MayThrowMove> v;
    const std::size_t n = fill_to_capacity(v, 4);
    REQUIRE(n >= 4);
    MayThrowMove x(static_cast<int>(n));
    CounterScope scope;
    v.push_back(std::move(x));
    const Counts d = scope.delta();
    REQUIRE(v.size() == n + 1);
    CHECK(first_mismatch(v, static_cast<int>(n) + 1) == -1);
    CHECK_MESSAGE(d.copy_ctor == static_cast<long>(n),
                  "expected the " << n << " old elements to be COPIED (their move may throw), saw " << to_string(d)
                                  << "\n-> relocate with std::move_if_noexcept(old[i]).");
    CHECK_MESSAGE(d.move_ctor == 1, "expected exactly one move construction (the new element from std::move(x)), saw "
                                        << to_string(d));
}

CPPATLAS_TEST("a move that may throw is never used to relocate a copyable type") {
    // With a correct vector no move constructor runs, so the armed throw never fires.
    vector<MayThrowMove> v;
    const std::size_t n = fill_to_capacity(v, 4);
    REQUIRE(n >= 4);
    const MayThrowMove x(static_cast<int>(n));
    {
        ThrowOn arm(Op::move_construct, 1);
        CHECK_NOTHROW(v.push_back(x));
        CHECK_MESSAGE(!arm.fired(), "a MayThrowMove element was MOVED during reallocation"
                                    "\n-> if the 3rd of 5 moves throws, two originals are already gutted and the "
                                    "vector cannot be restored. Use std::move_if_noexcept, which copies here.");
        CHECK_NOTHROW(v.reserve(4 * v.capacity() + 8));
        CHECK(!arm.fired());
    }
    REQUIRE(v.size() == n + 1);
    CHECK(first_mismatch(v, static_cast<int>(n) + 1) == -1);
}

CPPATLAS_TEST("a move-only type whose move may throw is still moved during relocation") {
    vector<MoveOnlyMayThrow> v;
    for (int i = 0; i < 5; ++i) v.emplace_back(i);
    REQUIRE(v.size() == 5);
    CounterScope scope;
    v.reserve(v.capacity() + 20);
    VERIFY(expect::relocated_by_move(scope.delta(), 5));
    REQUIRE(v.size() == 5);
    CHECK(first_mismatch(v, 5) == -1);
}

CPPATLAS_TEST("a move-only type whose move throws mid-relocation: basic guarantee (valid, no leaks)") {
    vector<MoveOnlyMayThrow> v;
    for (int i = 0; i < 6; ++i) v.emplace_back(i);
    REQUIRE(v.size() == 6);
    const std::size_t want = v.capacity() + 20;
    bool threw = false;
    {
        ThrowOn arm(Op::move_construct, 3);
        try {
            v.reserve(want);
        } catch (const InjectedError&) {
            threw = true;
        }
        CHECK(arm.fired());
    }
    CHECK_MESSAGE(threw, "the exception from the element's move constructor must propagate to the caller");
    // Only the basic guarantee: some elements may be moved-from, but the vector
    // must be consistent - exactly size() live elements, all usable.
    VERIFY(expect::live(static_cast<long>(v.size()),
                        "after the failed reserve, every live element must belong to the vector: destroy the "
                        "elements already moved into the new buffer before freeing it"));
    VERIFY(expect::lifetime_clean());
    CHECK(v.capacity() >= v.size());
    v.emplace_back(100);  // still usable
    CHECK(v.size() >= 1);
}

CPPATLAS_TEST("after many failed reallocations the vector still grows normally") {
    vector<MayThrowMove> v;
    for (int round = 0; round < 3; ++round) {
        const std::size_t n = fill_to_capacity(v, v.size() + 1);
        {
            ThrowOn arm(Op::copy_construct, static_cast<long>(n / 2) + 1);
            CHECK_THROWS_AS(v.emplace_back(-1), InjectedError);
        }
        REQUIRE(v.size() == n);
    }
    const std::size_t n = v.size();
    for (int i = 0; i < 50; ++i) v.emplace_back(static_cast<int>(n) + i);
    REQUIRE(v.size() == n + 50);
    CHECK(first_mismatch(v, static_cast<int>(n) + 50) == -1);
}
