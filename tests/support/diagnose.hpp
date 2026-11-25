// tests/support/diagnose.hpp - checks that EXPLAIN what went wrong.
//
// A failing check should tell the learner what their vector probably does
// wrong, not just "3 != 0". The pattern:
//
//     cppatlas::CounterScope scope;
//     v.reserve(16);
//     VERIFY(expect::relocated_by_move(scope.delta(), 3));
//
// VERIFY(verdict) records a doctest check at the CALLER's file:line. On
// failure it prints the verdict's message as an ERROR line, e.g.
//
//     stage_03.cpp:57: ERROR: reallocation relocated 3 elements: expected 3
//     move constructions and 0 copies, saw copy_ctor=3 move_ctor=0.
//     -> Tracked has a noexcept move constructor, so relocation must move ...
//
// REQUIRE_VERIFY(verdict) is the same but aborts the test case on failure (use
// it when continuing would be meaningless or unsafe).
//
// Verdict factories live in cppatlas::expect. Write new ones here (not inline
// in a stage) when a diagnosis could be reused by another stage. A factory is a
// plain function returning Verdict{ok, message}; build messages with
// cppatlas::Message (an ostringstream wrapper):  Message() << "saw " << n;
//
// Every message ends with a "-> " line that suggests the likely cause.
#pragma once

#include <cstddef>
#include <initializer_list>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>  // harness-internal bookkeeping only (Snapshot), never the subject

#include "doctest.h"
#include "tracked.hpp"

namespace cppatlas {

struct Verdict {
    bool ok = true;
    std::string message;

    static Verdict pass() { return {true, {}}; }
    static Verdict fail(std::string msg) { return {false, std::move(msg)}; }
    explicit operator bool() const noexcept { return ok; }
};

// Tiny string builder: Message() << "a" << 1 << "b"  converts to std::string.
class Message {
public:
    template <class T>
    Message& operator<<(const T& v) {
        os_ << v;
        return *this;
    }
    std::string str() const { return os_.str(); }
    operator std::string() const { return str(); }  // NOLINT(google-explicit-constructor)

private:
    std::ostringstream os_;
};

namespace expect {

// Generic: exactly `expected` occurrences of `op` in the window `d`.
inline Verdict ops(const Counts& d, Op op, long expected, std::string_view hint = {}) {
    const long got = d.get(op);
    if (got == expected) return Verdict::pass();
    Message m;
    m << "expected " << expected << " x " << op_name(op) << ", saw " << got << " (all element operations in this window: "
      << to_string(d) << ")";
    if (!hint.empty()) m << "\n-> " << hint;
    return Verdict::fail(m);
}

// The window `d` must not contain any constructor, assignment or destructor
// call. `what` names the operation under test ("moving the vector").
inline Verdict no_element_ops(const Counts& d, std::string_view what, std::string_view hint = {}) {
    if (d.element_ops() == 0) return Verdict::pass();
    Message m;
    m << what << " must not touch any element, but it performed: " << to_string(d);
    if (!hint.empty())
        m << "\n-> " << hint;
    else if (d.default_ctor > 0)
        m << "\n-> elements were default-constructed: allocate raw storage instead of `new T[n]`.";
    return Verdict::fail(m);
}

// T must never be default-constructed by the vector itself.
inline Verdict no_default_constructions(const Counts& d) {
    if (d.default_ctor == 0) return Verdict::pass();
    return Verdict::fail(Message()
                         << "T was default-constructed " << d.default_ctor
                         << " time(s), but nothing in this test asks for a default-constructed T."
                         << "\n-> allocate raw, uninitialized storage (std::allocator<T>().allocate(n) or "
                            "::operator new(n * sizeof(T))) instead of `new T[n]`, and construct each element "
                            "in place with std::construct_at(p, value) or `new (p) T(value)`.");
}

// No objects may be alive "now" (call it after the vector went out of scope).
inline Verdict all_destroyed(std::string_view when = "after the vector was destroyed") {
    const long live = counts().live;
    if (live == 0) return Verdict::pass();
    detail::ledger().reported_live = true;
    Message m;
    if (live > 0) {
        m << live << " element object(s) still alive " << when << "."
          << "\n-> does ~vector() destroy every element (std::destroy_at(p) / p->~T() for the first size() slots) "
             "BEFORE freeing the storage? Freeing memory does not run destructors.";
    } else {
        m << "more elements were destroyed than constructed (live = " << live << ") " << when << "."
          << "\n-> an element is being destroyed twice, or a slot that never held an element is destroyed.";
    }
    return Verdict::fail(m);
}

// Exactly `expected` objects alive right now.
inline Verdict live(long expected, std::string_view hint = {}) {
    const long got = counts().live;
    if (got == expected) return Verdict::pass();
    Message m;
    m << "expected " << expected << " live element object(s), found " << got << ".";
    if (!hint.empty()) m << "\n-> " << hint;
    return Verdict::fail(m);
}

// In a closed window (everything created inside it is gone again), every
// construction is matched by exactly one destruction.
inline Verdict destroyed_exactly_once(const Counts& d) {
    // Objects "adopted" by an assignment into raw memory count as created.
    const long created = d.constructions() + d.assign_to_raw;
    if (created == d.dtor && d.bad_destroy == 0) return Verdict::pass();
    Message m;
    m << created << " element(s) were created but " << d.dtor << " destructor call(s) ran on live objects";
    if (d.bad_destroy) m << " (+" << d.bad_destroy << " on dead/raw memory)";
    m << " (" << to_string(d) << ").";
    if (d.bad_destroy > 0)
        m << "\n-> some elements were destroyed more than once, or slots past size() were destroyed.";
    else if (d.dtor < created)
        m << "\n-> some elements were never destroyed: ~vector() (and reallocation, for the old buffer) must "
             "destroy each element exactly once.";
    else
        m << "\n-> more destructor calls than objects: an element is destroyed twice.";
    return Verdict::fail(m);
}

// No double-destroy / use-after-destroy / assignment into raw memory so far.
inline Verdict lifetime_clean() {
    const Counts& c = counts();
    if (c.lifetime_errors() == 0) return Verdict::pass();
    detail::ledger().reported_lifetime = true;
    Message m;
    m << "object lifetime error(s) detected: ";
    if (c.bad_destroy) m << c.bad_destroy << " bad destroy ";
    if (c.use_after_destroy) m << c.use_after_destroy << " use after destroy ";
    if (c.assign_to_raw) m << c.assign_to_raw << " assignment into raw memory ";
    m << "\nfirst one: " << first_lifetime_error();
    if (c.assign_to_raw)
        m << "\n-> `data_[i] = value` on uninitialized storage is undefined behaviour: use "
             "std::construct_at(data_ + i, value) (or placement new) to create the element.";
    else if (c.bad_destroy)
        m << "\n-> destroy each element exactly once, and only the first size() slots.";
    else
        m << "\n-> an element was used after its destructor ran: keep the old buffer's elements alive until "
             "they have been copied/moved into the new buffer, then destroy them, then free the buffer. "
             "Or a slot past size() was read: check end() / index arithmetic.";
    return Verdict::fail(m);
}

// Reallocation of `n` existing elements of a nothrow-movable type: n move
// constructions, no copies (std::move_if_noexcept picks move).
// The window `d` must contain ONLY the relocation (e.g. wrap a reserve()
// call); a push_back in the window adds its own copy/move of the new element.
inline Verdict relocated_by_move(const Counts& d, long n) {
    if (d.move_ctor == n && d.copies() == 0) return Verdict::pass();
    Message m;
    m << "relocating " << n << " element(s): expected " << n << " move constructions and 0 copies, saw "
      << to_string(d) << ".";
    if (d.copy_ctor > 0 && d.move_ctor == 0)
        m << "\n-> the elements were copied. Their move constructor is noexcept, so relocation should move "
             "them: construct the new slot with std::move_if_noexcept(old[i]).";
    else if (d.move_assign > 0 || d.copy_assign > 0)
        m << "\n-> elements were ASSIGNED into the new buffer; the new buffer is raw memory, so construct "
             "into it (std::construct_at) instead.";
    else if (d.move_ctor > n)
        m << "\n-> more moves than elements: is something relocated twice, or reallocated more than once?";
    return Verdict::fail(m);
}

// Reallocation of `n` existing elements whose move constructor may throw:
// n copy constructions, no moves (strong exception guarantee).
inline Verdict relocated_by_copy(const Counts& d, long n) {
    if (d.copy_ctor == n && d.moves() == 0) return Verdict::pass();
    Message m;
    m << "relocating " << n << " element(s) whose move constructor is NOT noexcept: expected " << n
      << " copy constructions and 0 moves, saw " << to_string(d) << ".";
    if (d.move_ctor > 0)
        m << "\n-> moving such elements loses the strong exception guarantee (if the k-th move throws, the "
             "first k-1 originals are already gutted). Use std::move_if_noexcept(old[i]): it yields a "
             "const T& (copy) exactly when T's move may throw and T is copyable.";
    return Verdict::fail(m);
}


// Reallocation of `n` elements by ANY means (copy or move, the learner's
// choice before stage 6): exactly n copy/move constructions into the new
// buffer, n destructions of the old elements, no assignments, nothing else.
// The window must contain only the relocation (e.g. wrap a reserve() call).
inline Verdict relocated(const Counts& d, long n, std::string_view what = "reallocation") {
    const long relocations = d.copy_ctor + d.move_ctor;
    if (relocations == n && d.dtor == n && d.assignments() == 0 && d.default_ctor == 0 && d.value_ctor == 0)
        return Verdict::pass();
    Message m;
    m << what << " of " << n << " element(s): expected " << n << " copy/move constructions into the new buffer and "
      << n << " destructions of the old elements, nothing else; saw " << to_string(d) << ".";
    if (d.default_ctor > 0)
        m << "\n-> elements were default-constructed: allocate raw storage instead of `new T[n]`.";
    else if (d.assignments() > 0)
        m << "\n-> elements were ASSIGNED into the new buffer; it is raw memory, so construct into it "
             "(std::construct_at(new_data + i, ...)) instead.";
    else if (d.dtor < n)
        m << "\n-> the old elements were not all destroyed: after relocating, destroy each old element "
             "(std::destroy_at) before freeing the old buffer.";
    else if (d.dtor > n)
        m << "\n-> more destructions than elements: something is destroyed twice (or slots past size() are).";
    else if (relocations > n)
        m << "\n-> more constructions than elements: relocate each element exactly once, directly into the new "
             "buffer.";
    else
        m << "\n-> every element must be copied or moved into the new buffer exactly once.";
    return Verdict::fail(m);
}

// No copy construction and no copy assignment in `d`.
inline Verdict no_copies(const Counts& d, std::string_view what, std::string_view hint = {}) {
    if (d.copies() == 0) return Verdict::pass();
    Message m;
    m << what << " must not copy any element, but it performed: " << to_string(d);
    if (!hint.empty()) m << "\n-> " << hint;
    return Verdict::fail(m);
}

// `n` allocations (`bytes` bytes in total) made during a test were still not
// freed when it ended (alloc_check.hpp measures it; preflight.hpp reports it).
inline Verdict allocations_freed(long n, std::size_t bytes) {
    if (n <= 0) return Verdict::pass();
    return Verdict::fail(Message()
                         << n << " allocation(s) (" << bytes << " bytes) made during this test were never freed, "
                         << "although every element was destroyed."
                         << "\n-> memory leak: free the old buffer after relocating into a new one (reserve / "
                            "growth), free the buffer in ~vector(), and free the new buffer on the error path when "
                            "constructing into it throws. Every allocate(n) needs a matching deallocate(p, n).");
}

// The first failing verdict of the list, or pass. For a group of checks that
// would all fail for the same reason: report only the first one.
inline Verdict first_failure(std::initializer_list<Verdict> verdicts) {
    for (const Verdict& v : verdicts)
        if (!v.ok) return v;
    return Verdict::pass();
}

// `got` must not exceed `bound`. `what` names the quantity.
inline Verdict at_most(long got, long bound, std::string_view what, std::string_view hint = {}) {
    if (got <= bound) return Verdict::pass();
    Message m;
    m << what << ": expected at most " << bound << ", saw " << got << ".";
    if (!hint.empty()) m << "\n-> " << hint;
    return Verdict::fail(m);
}

}  // namespace expect

// ---------------------------------------------------------------------------
// Snapshot of a vector of instrumented elements, for "observably unchanged"
// checks (strong exception guarantee, address stability).
//
//     REQUIRE(v.size() == 5);
//     const Snapshot before = snapshot(v);
//     CHECK_THROWS_AS(v.push_back(x), InjectedError);
//     VERIFY(expect::unchanged(before, v, "a push_back that threw"));
//
// Records size(), capacity(), the address of the first element and the id and
// value of every element (ids change on every copy/move, so "same ids" means
// "the very same objects"). Uses only size(), capacity() and operator[] const.
// ---------------------------------------------------------------------------
struct Snapshot {
    std::size_t size = 0;
    std::size_t capacity = 0;
    const void* first = nullptr;  // &v[0], or nullptr when empty
    std::vector<long> ids;
    std::vector<int> values;  // value(), or kMovedFrom for moved-from elements
    static constexpr int kMovedFrom = -1'000'000;
};

template <class V>
Snapshot snapshot(const V& v) {
    Snapshot s;
    s.size = v.size();
    s.capacity = v.capacity();
    if (s.size > 0) s.first = static_cast<const void*>(&v[0]);
    s.ids.reserve(s.size);
    s.values.reserve(s.size);
    for (std::size_t i = 0; i < s.size; ++i) {
        const auto& e = v[i];
        s.ids.push_back(e.id());
        s.values.push_back(e.moved_from() ? Snapshot::kMovedFrom : e.value());
    }
    return s;
}

namespace expect {

// The vector `v` is observably identical to `before`: same size, capacity,
// buffer address, and the very same element objects (ids) with the same values.
// Reads elements only if the size matches, so a broken vector cannot make it
// index out of bounds.
template <class V>
Verdict unchanged(const Snapshot& before, const V& v, std::string_view what, std::string_view hint = {}) {
    Message m;
    bool ok = true;
    const std::size_t size = v.size();
    const std::size_t cap = v.capacity();
    if (size != before.size) {
        ok = false;
        m << "size() changed from " << before.size << " to " << size << ". ";
    }
    if (cap != before.capacity) {
        ok = false;
        m << "capacity() changed from " << before.capacity << " to " << cap << ". ";
    }
    if (ok && size > 0) {
        if (static_cast<const void*>(&v[0]) != before.first) {
            ok = false;
            m << "the elements moved to a different buffer (&v[0] was " << before.first << ", is now "
              << static_cast<const void*>(&v[0]) << "). ";
        }
        for (std::size_t i = 0; i < size; ++i) {
            const auto& e = v[i];
            const bool alive = e.looks_alive();
            const int value = !alive ? Snapshot::kMovedFrom - 1 : e.moved_from() ? Snapshot::kMovedFrom : e.value();
            if (!alive || e.id() != before.ids[i] || value != before.values[i]) {
                ok = false;
                m << "v[" << i << "] was " << e << ", expected the original object (id " << before.ids[i]
                  << ", value ";
                if (before.values[i] == Snapshot::kMovedFrom)
                    m << "moved-from";
                else
                    m << before.values[i];
                m << "). ";
                break;
            }
        }
    }
    if (ok) return Verdict::pass();
    Message out;
    out << what << " must leave the vector exactly as it was, but: " << m.str();
    if (!hint.empty()) out << "\n-> " << hint;
    return Verdict::fail(out);
}

}  // namespace expect

}  // namespace cppatlas

#define CPPATLAS_VERIFY(...)                                                \
    do {                                                                    \
        const ::cppatlas::Verdict cppatlas_verdict_ = (__VA_ARGS__);        \
        if (cppatlas_verdict_.ok) {                                         \
            CHECK(true);                                                    \
        } else {                                                            \
            FAIL_CHECK(cppatlas_verdict_.message);                          \
        }                                                                   \
    } while (false)

#define CPPATLAS_REQUIRE_VERIFY(...)                                        \
    do {                                                                    \
        const ::cppatlas::Verdict cppatlas_verdict_ = (__VA_ARGS__);        \
        if (cppatlas_verdict_.ok) {                                         \
            CHECK(true);                                                    \
        } else {                                                            \
            FAIL(cppatlas_verdict_.message);                                \
        }                                                                   \
    } while (false)

#if !defined(CPPATLAS_NO_SHORT_MACROS)
#define VERIFY(...) CPPATLAS_VERIFY(__VA_ARGS__)
#define REQUIRE_VERIFY(...) CPPATLAS_REQUIRE_VERIFY(__VA_ARGS__)
#endif
