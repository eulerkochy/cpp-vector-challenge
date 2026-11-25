// tests/support/tracked.hpp - instrumented element types, counters, exception injection.
//
// ---------------------------------------------------------------------------
// COUNTERS
// ---------------------------------------------------------------------------
// Every instrumented type below reports into ONE process-wide ledger
// (cppatlas::counts()). The ledger is reset automatically at the start of
// every CPPATLAS_TEST, so state never leaks between test cases.
//
//     cppatlas::Counts      plain struct of counters (see fields below)
//     cppatlas::counts()    current totals since the last reset
//     cppatlas::reset()     zero everything (done for you at each test start)
//     cppatlas::clear_lifetime_errors()   forgive deliberately provoked errors
//     cppatlas::CounterScope scope;   // snapshot "now"
//     ...code under test...
//     cppatlas::Counts d = scope.delta();   // what happened since the snapshot
//     CHECK(d.move_ctor == 3); CHECK(d.copies() == 0);
//
// `live` is a level, not an event count: counts().live is the number of
// instrumented objects alive right now; delta().live is the net change.
//
// ---------------------------------------------------------------------------
// ELEMENT TYPES  (all have value(), id(), moved_from(), == int, operator<<)
// ---------------------------------------------------------------------------
//   Tracked        default ctor, explicit Tracked(int), copyable, noexcept move.
//                  The workhorse. Alias: NoexceptMove.
//   MayThrowMove   like Tracked but the move ctor/assign are NOT noexcept, so a
//                  correct vector must COPY it during reallocation
//                  (std::move_if_noexcept).
//   CopyOnly       copyable, no move constructor at all: rvalues are copied.
//   MoveOnly       move-only (copy ctor/assign unavailable), noexcept move.
//   MoveOnlyMayThrow  move-only AND the move may throw: std::move_if_noexcept
//                  still moves it (there is no copy to fall back to), so only
//                  the basic exception guarantee is possible.
//   NoDefault      no default constructor (deleted with an explanatory
//                  message), otherwise like Tracked. Catches `new T[n]`.
//   MultiArg       explicit multi-argument constructors to verify perfect
//                  forwarding in emplace_back; records how each ArgProbe
//                  argument arrived (lvalue / const lvalue / rvalue).
//
// Compare against ints, not against temporaries, so the comparison itself does
// not show up in the counters:   CHECK(v[0] == 42);   // not == Tracked(42)
//
// A moved-from object keeps its id, reports moved_from() == true, compares
// unequal to every int, and prints as `Tracked#7{moved-from}`.
//
// ---------------------------------------------------------------------------
// LIFETIME ERROR DETECTION (cheap, magic-field based)
// ---------------------------------------------------------------------------
//   bad_destroy        destructor ran on an object that was already destroyed,
//                      or on raw memory that never held an object
//                      (destroying capacity() elements instead of size()).
//   use_after_destroy  value() / copy / move / assign FROM a destroyed object.
//   assign_to_raw      copy/move-ASSIGNED into memory holding no live object
//                      (`data_[size_] = x` on raw storage instead of
//                      std::construct_at / placement new). The target is then
//                      adopted as live so the other counters stay meaningful.
// The first error's description is kept in cppatlas::first_lifetime_error().
// These are heuristics on memory the vector owns; AddressSanitizer covers
// freed memory precisely.
//
// ---------------------------------------------------------------------------
// EXCEPTION INJECTION
// ---------------------------------------------------------------------------
//     {
//         cppatlas::ThrowOn arm(cppatlas::Op::copy_construct, 3);  // 3rd copy ctor throws
//         CHECK_THROWS_AS(v.reserve(10), cppatlas::InjectedError);
//         CHECK(arm.fired());
//     }   // disarmed here
// - Counting starts when the guard is constructed; it fires ONCE, then disarms
//   itself (so cleanup code never hits a second injected throw).
// - Injectable ops: value_construct, copy_construct, move_construct,
//   copy_assign, move_assign (also default_construct). Not destroy.
// - Injection on move_construct / move_assign is IGNORED by types whose move is
//   noexcept (Tracked, MoveOnly, NoDefault, MultiArg): throwing there would
//   call std::terminate. Use MayThrowMove (or MoveOnlyMayThrow) to inject on moves.
// - A throwing constructor does not count as a construction (no counter
//   increment, no live object); counts().injected counts fired throws.
#pragma once

#include <cstdint>
#include <memory>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "trace.hpp"

namespace cppatlas {

// ---------------------------------------------------------------------------
// Operations and counters
// ---------------------------------------------------------------------------

enum class Op : int {
    default_construct,
    value_construct,
    copy_construct,
    move_construct,
    copy_assign,
    move_assign,
    destroy,
};

inline constexpr const char* op_name(Op op) noexcept {
    switch (op) {
    case Op::default_construct: return "default_construct";
    case Op::value_construct: return "value_construct";
    case Op::copy_construct: return "copy_construct";
    case Op::move_construct: return "move_construct";
    case Op::copy_assign: return "copy_assign";
    case Op::move_assign: return "move_assign";
    case Op::destroy: return "destroy";
    }
    return "?";
}

struct Counts {
    // element operations (successful ones)
    long default_ctor = 0;
    long value_ctor = 0;  // any non-special constructor: Tracked(int), MultiArg(...)
    long copy_ctor = 0;
    long move_ctor = 0;
    long copy_assign = 0;
    long move_assign = 0;
    long dtor = 0;
    // level: objects alive now (or net change, in a delta)
    long live = 0;
    // lifetime errors (should always be 0)
    long bad_destroy = 0;
    long use_after_destroy = 0;
    long assign_to_raw = 0;
    // exceptions thrown by ThrowOn
    long injected = 0;

    constexpr long constructions() const noexcept {
        return default_ctor + value_ctor + copy_ctor + move_ctor;
    }
    constexpr long copies() const noexcept { return copy_ctor + copy_assign; }
    constexpr long moves() const noexcept { return move_ctor + move_assign; }
    constexpr long assignments() const noexcept { return copy_assign + move_assign; }
    // every constructor, assignment and destructor call
    constexpr long element_ops() const noexcept {
        return constructions() + assignments() + dtor;
    }
    constexpr long lifetime_errors() const noexcept {
        return bad_destroy + use_after_destroy + assign_to_raw;
    }
    constexpr long get(Op op) const noexcept {
        switch (op) {
        case Op::default_construct: return default_ctor;
        case Op::value_construct: return value_ctor;
        case Op::copy_construct: return copy_ctor;
        case Op::move_construct: return move_ctor;
        case Op::copy_assign: return copy_assign;
        case Op::move_assign: return move_assign;
        case Op::destroy: return dtor;
        }
        return 0;
    }

    friend constexpr Counts operator-(Counts a, const Counts& b) noexcept {
        a.default_ctor -= b.default_ctor;
        a.value_ctor -= b.value_ctor;
        a.copy_ctor -= b.copy_ctor;
        a.move_ctor -= b.move_ctor;
        a.copy_assign -= b.copy_assign;
        a.move_assign -= b.move_assign;
        a.dtor -= b.dtor;
        a.live -= b.live;
        a.bad_destroy -= b.bad_destroy;
        a.use_after_destroy -= b.use_after_destroy;
        a.assign_to_raw -= b.assign_to_raw;
        a.injected -= b.injected;
        return a;
    }
    friend constexpr bool operator==(const Counts&, const Counts&) = default;
};

// "copy_ctor=3 move_ctor=2 dtor=5 live=0" - only non-zero fields (and live).
inline std::string to_string(const Counts& c) {
    std::ostringstream os;
    auto field = [&](const char* name, long v) {
        if (v != 0) os << name << '=' << v << ' ';
    };
    field("default_ctor", c.default_ctor);
    field("value_ctor", c.value_ctor);
    field("copy_ctor", c.copy_ctor);
    field("move_ctor", c.move_ctor);
    field("copy_assign", c.copy_assign);
    field("move_assign", c.move_assign);
    field("dtor", c.dtor);
    os << "live=" << c.live;
    if (c.bad_destroy) os << " bad_destroy=" << c.bad_destroy;
    if (c.use_after_destroy) os << " use_after_destroy=" << c.use_after_destroy;
    if (c.assign_to_raw) os << " assign_to_raw=" << c.assign_to_raw;
    if (c.injected) os << " injected=" << c.injected;
    return os.str();
}

inline std::ostream& operator<<(std::ostream& os, const Counts& c) {
    return os << '{' << to_string(c) << '}';
}

// ---------------------------------------------------------------------------
// Exceptions
// ---------------------------------------------------------------------------

// The only exception type the harness ever throws into the vector under test.
class InjectedError : public std::runtime_error {
public:
    InjectedError(Op op, long nth)
        : std::runtime_error(std::string("cppatlas::InjectedError: injected throw on ") +
                             op_name(op) + " #" + std::to_string(nth)),
          op_(op) {}
    Op op() const noexcept { return op_; }

private:
    Op op_;
};

// ---------------------------------------------------------------------------
// The ledger (implementation detail; use counts()/reset()/CounterScope)
// ---------------------------------------------------------------------------

namespace detail {

inline constexpr int kOpCount = 7;

struct Ledger {
    Counts counts;
    long next_id = 1;  // never reset, so ids are unique per process
    std::string first_error;
    // set when a leak / lifetime verdict already failed in this test, so the
    // automatic end-of-test checks do not repeat the same diagnosis
    bool reported_live = false;
    bool reported_lifetime = false;
    long countdown[kOpCount] = {};  // 0 = disarmed, k = throw on the k-th next op
    long armed_nth[kOpCount] = {};
    bool fired[kOpCount] = {};
};

inline Ledger& ledger() noexcept {
    static Ledger l;
    return l;
}

inline constexpr std::uint32_t kLive = 0xA11CE5EDu;
inline constexpr std::uint32_t kDead = 0xDEADBEEFu;

// Volatile accesses so the optimizer cannot drop the "dead" store in a
// destructor (GCC's -flifetime-dse would otherwise remove it).
inline std::uint32_t load_magic(const std::uint32_t& m) noexcept {
    return *static_cast<const volatile std::uint32_t*>(&m);
}
inline void store_magic(std::uint32_t& m, std::uint32_t v) noexcept {
    *static_cast<volatile std::uint32_t*>(&m) = v;
}

inline void record_error(long Counts::*field, const char* what, const std::string& detail_text) {
    Ledger& l = ledger();
    ++(l.counts.*field);
    if (l.first_error.empty()) l.first_error = detail_text;
    trace::error(what, detail_text);
}

inline void maybe_throw(Op op) {
    Ledger& l = ledger();
    long& c = l.countdown[static_cast<int>(op)];
    if (c > 0 && --c == 0) {
        l.fired[static_cast<int>(op)] = true;
        ++l.counts.injected;
        trace::element("injected_throw", op_name(op), 0, nullptr, 0);
        throw InjectedError(op, l.armed_nth[static_cast<int>(op)]);
    }
}

}  // namespace detail

inline const Counts& counts() noexcept { return detail::ledger().counts; }

// Description of the first lifetime error since the last reset ("" if none).
inline const std::string& first_lifetime_error() noexcept { return detail::ledger().first_error; }

// Forget recorded lifetime errors (counters bad_destroy / use_after_destroy /
// assign_to_raw and first_lifetime_error()). For tests that provoke errors on
// purpose, so the automatic end-of-test checks do not fail.
inline void clear_lifetime_errors() noexcept {
    detail::Ledger& l = detail::ledger();
    l.counts.bad_destroy = 0;
    l.counts.use_after_destroy = 0;
    l.counts.assign_to_raw = 0;
    std::string().swap(l.first_error);  // release the memory too (alloc_check.hpp)
    l.reported_lifetime = false;
}

// Zero ALL counters (including `live`), forget errors, disarm all injection.
// Called automatically at the start of each CPPATLAS_TEST. Do not call it while
// instrumented objects are alive (their later destruction would drive `live`
// negative); use CounterScope for baselines and clear_lifetime_errors() to
// forgive deliberate errors.
inline void reset() noexcept {
    detail::Ledger& l = detail::ledger();
    l.counts = Counts{};
    std::string().swap(l.first_error);  // release the memory too (alloc_check.hpp)
    l.reported_live = false;
    l.reported_lifetime = false;
    for (int i = 0; i < detail::kOpCount; ++i) {
        l.countdown[i] = 0;
        l.armed_nth[i] = 0;
        l.fired[i] = false;
    }
}

// RAII snapshot: delta() is everything that happened since construction (or
// the last restart()).
class CounterScope {
public:
    CounterScope() noexcept : start_(counts()) {}
    Counts delta() const noexcept { return counts() - start_; }
    const Counts& start() const noexcept { return start_; }
    void restart() noexcept { start_ = counts(); }

private:
    Counts start_;
};

// RAII exception injection: the nth (1-based) `op` after construction throws
// InjectedError once. Disarmed by the destructor or disarm().
class ThrowOn {
public:
    ThrowOn(Op op, long nth) : op_(op) {
        if (op == Op::destroy) throw std::logic_error("cppatlas::ThrowOn: destructors never throw");
        if (nth < 1) throw std::logic_error("cppatlas::ThrowOn: nth is 1-based");
        detail::Ledger& l = detail::ledger();
        l.countdown[idx()] = nth;
        l.armed_nth[idx()] = nth;
        l.fired[idx()] = false;
    }
    ~ThrowOn() { disarm(); }
    ThrowOn(const ThrowOn&) = delete;
    ThrowOn& operator=(const ThrowOn&) = delete;

    void disarm() noexcept { detail::ledger().countdown[idx()] = 0; }
    bool fired() const noexcept { return detail::ledger().fired[idx()]; }

private:
    int idx() const noexcept { return static_cast<int>(op_); }
    Op op_;
};

// ---------------------------------------------------------------------------
// Instrumented element type
// ---------------------------------------------------------------------------

// `= delete("reason")` (C++26 P2573) is accepted as an extension in C++20/C++23 mode
// by GCC >= 15 and Clang >= 19 and makes `new T[n]` errors self-explanatory.
#if (defined(__clang__) && __clang_major__ >= 19) || \
    (defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 15)
#define CPPATLAS_DELETED_BECAUSE(msg) delete (msg)
#define CPPATLAS_DELETED_PUSH                                              \
    _Pragma("GCC diagnostic push")                                         \
    _Pragma("GCC diagnostic ignored \"-Wpragmas\"")                        \
    _Pragma("GCC diagnostic ignored \"-Wunknown-warning-option\"")         \
    _Pragma("GCC diagnostic ignored \"-Wc++26-extensions\"")               \
    _Pragma("GCC diagnostic ignored \"-Wc++2c-extensions\"")
#define CPPATLAS_DELETED_POP _Pragma("GCC diagnostic pop")
#else
#define CPPATLAS_DELETED_BECAUSE(msg) delete
#define CPPATLAS_DELETED_PUSH
#define CPPATLAS_DELETED_POP
#endif

struct TrackedTraits {
    bool default_ctor = true;
    bool copyable = true;
    bool movable = true;
    bool noexcept_move = true;
    int tag = 0;  // distinguishes otherwise-identical types (MultiArg)
};

namespace detail {
template <TrackedTraits Tr>
struct tracked_name {
    static constexpr const char* value = "Tracked";
};
}  // namespace detail

template <TrackedTraits Tr>
class BasicTracked {
public:
    static constexpr const char* type_name() noexcept { return detail::tracked_name<Tr>::value; }

    BasicTracked() requires(Tr.default_ctor) : value_(0) {
        detail::maybe_throw(Op::default_construct);
        born(Op::default_construct, 0);
    }
    CPPATLAS_DELETED_PUSH
    BasicTracked() requires(!Tr.default_ctor) = CPPATLAS_DELETED_BECAUSE(
        "this element type has no default constructor. my::vector must never default-construct "
        "T: allocate raw, uninitialized storage (std::allocator<T>::allocate or ::operator new) "
        "instead of `new T[n]`, and construct elements in place with std::construct_at or "
        "placement new.");
    CPPATLAS_DELETED_POP

    explicit BasicTracked(int v) : value_(v) {
        detail::maybe_throw(Op::value_construct);
        born(Op::value_construct, 0);
    }

    BasicTracked(const BasicTracked& o) requires(Tr.copyable)
        : value_(o.value_), moved_from_(o.moved_from_) {
        o.check_alive("copied from");
        detail::maybe_throw(Op::copy_construct);
        born(Op::copy_construct, o.id_);
    }

    BasicTracked(BasicTracked&& o) noexcept(Tr.noexcept_move) requires(Tr.movable)
        : value_(o.value_), moved_from_(o.moved_from_) {
        o.check_alive("moved from");
        if constexpr (!Tr.noexcept_move) detail::maybe_throw(Op::move_construct);
        born(Op::move_construct, o.id_);
        o.value_ = kMovedFromValue;
        o.moved_from_ = true;
    }

    BasicTracked& operator=(const BasicTracked& o) requires(Tr.copyable) {
        o.check_alive("copy-assigned from");
        detail::maybe_throw(Op::copy_assign);
        adopt_if_raw("copy-assigned");
        value_ = o.value_;
        moved_from_ = o.moved_from_;
        ++detail::ledger().counts.copy_assign;
        trace::element("copy_assign", type_name(), id_, this, value_, o.id_);
        return *this;
    }

    BasicTracked& operator=(BasicTracked&& o) noexcept(Tr.noexcept_move) requires(Tr.movable) {
        o.check_alive("move-assigned from");
        if constexpr (!Tr.noexcept_move) detail::maybe_throw(Op::move_assign);
        adopt_if_raw("move-assigned");
        if (this != &o) {
            value_ = o.value_;
            moved_from_ = o.moved_from_;
            o.value_ = kMovedFromValue;
            o.moved_from_ = true;
        }
        ++detail::ledger().counts.move_assign;
        trace::element("move_assign", type_name(), id_, this, value_, o.id_);
        return *this;
    }

    ~BasicTracked() {
        if (detail::load_magic(magic_) != detail::kLive) {
            detail::record_error(&Counts::bad_destroy, "bad_destroy",
                                 describe_raw("destructor ran on") +
                                     " that was already destroyed or never constructed. Every "
                                     "element must be destroyed exactly once, and only the "
                                     "first size() slots hold elements (not capacity()).");
            return;  // do not touch `live`: it was not alive
        }
        detail::store_magic(magic_, detail::kDead);
        detail::Ledger& l = detail::ledger();
        ++l.counts.dtor;
        --l.counts.live;
        trace::element("destroy", type_name(), id_, this, value_);
    }

    // The payload. Reading a destroyed object is recorded as use_after_destroy.
    int value() const {
        check_alive("read (value())");
        return value_;
    }
    long id() const noexcept { return id_; }
    bool moved_from() const noexcept { return moved_from_; }
    // True if the magic field says "alive" (heuristic; for diagnostics only).
    bool looks_alive() const noexcept { return detail::load_magic(magic_) == detail::kLive; }

    friend bool operator==(const BasicTracked& a, const BasicTracked& b) {
        return a.moved_from_ == b.moved_from_ && a.value() == b.value();
    }
    friend bool operator==(const BasicTracked& a, int v) { return !a.moved_from_ && a.value() == v; }

    friend std::ostream& operator<<(std::ostream& os, const BasicTracked& t) {
        os << type_name() << '#' << t.id_ << '{';
        if (!t.looks_alive())
            os << "DESTROYED";
        else if (t.moved_from_)
            os << "moved-from";
        else
            os << t.value_;
        return os << '}';
    }

    static constexpr int kMovedFromValue = -1'000'000;

protected:
    void born(Op op, long from_id) noexcept {
        detail::Ledger& l = detail::ledger();
        id_ = l.next_id++;
        detail::store_magic(magic_, detail::kLive);
        switch (op) {
        case Op::default_construct: ++l.counts.default_ctor; break;
        case Op::copy_construct: ++l.counts.copy_ctor; break;
        case Op::move_construct: ++l.counts.move_ctor; break;
        default: ++l.counts.value_ctor; break;
        }
        ++l.counts.live;
        trace::element(op_name(op), type_name(), id_, this, value_, from_id);
    }

private:
    std::string describe_raw(const char* verb) const {
        std::ostringstream os;
        os << verb << " a " << type_name() << " at " << static_cast<const void*>(this);
        return os.str();
    }

    void check_alive(const char* how) const {
        if (detail::load_magic(magic_) != detail::kLive) {
            detail::record_error(&Counts::use_after_destroy, "use_after_destroy",
                                 describe_raw("an element was") + " " + std::string(how) +
                                     " after it was destroyed (or before it was constructed).");
        }
    }

    void adopt_if_raw(const char* how) {
        if (detail::load_magic(magic_) == detail::kLive) return;
        detail::record_error(
            &Counts::assign_to_raw, "assign_to_raw",
            describe_raw(how) + " - but that memory held no live object. Raw storage must be "
                                "initialized with std::construct_at / placement new, not assigned "
                                "to (assignment needs an existing object on the left).");
        detail::Ledger& l = detail::ledger();
        id_ = l.next_id++;
        detail::store_magic(magic_, detail::kLive);
        ++l.counts.live;
    }

    int value_;
    bool moved_from_ = false;
    long id_ = 0;
    std::uint32_t magic_ = 0;
};

// The concrete element types.
inline constexpr TrackedTraits kTracked{};
inline constexpr TrackedTraits kMayThrowMove{.noexcept_move = false};
inline constexpr TrackedTraits kCopyOnly{.movable = false, .noexcept_move = false};
inline constexpr TrackedTraits kMoveOnly{.copyable = false};
inline constexpr TrackedTraits kNoDefault{.default_ctor = false};
inline constexpr TrackedTraits kMultiArg{.default_ctor = false, .tag = 1};
inline constexpr TrackedTraits kMoveOnlyMayThrow{.copyable = false, .noexcept_move = false};

namespace detail {
template <> struct tracked_name<kMayThrowMove> { static constexpr const char* value = "MayThrowMove"; };
template <> struct tracked_name<kCopyOnly> { static constexpr const char* value = "CopyOnly"; };
template <> struct tracked_name<kMoveOnly> { static constexpr const char* value = "MoveOnly"; };
template <> struct tracked_name<kNoDefault> { static constexpr const char* value = "NoDefault"; };
template <> struct tracked_name<kMultiArg> { static constexpr const char* value = "MultiArg"; };
template <> struct tracked_name<kMoveOnlyMayThrow> { static constexpr const char* value = "MoveOnlyMayThrow"; };
}  // namespace detail

using Tracked = BasicTracked<kTracked>;
using NoexceptMove = Tracked;
using MayThrowMove = BasicTracked<kMayThrowMove>;
using CopyOnly = BasicTracked<kCopyOnly>;
using MoveOnly = BasicTracked<kMoveOnly>;
using NoDefault = BasicTracked<kNoDefault>;
using MoveOnlyMayThrow = BasicTracked<kMoveOnlyMayThrow>;

// ---------------------------------------------------------------------------
// MultiArg: perfect-forwarding probe for emplace_back
// ---------------------------------------------------------------------------

enum class ArgKind { none, lvalue, const_lvalue, rvalue };

inline constexpr const char* arg_kind_name(ArgKind k) noexcept {
    switch (k) {
    case ArgKind::none: return "none";
    case ArgKind::lvalue: return "non-const lvalue";
    case ArgKind::const_lvalue: return "const lvalue";
    case ArgKind::rvalue: return "rvalue";
    }
    return "?";
}

inline std::ostream& operator<<(std::ostream& os, ArgKind k) { return os << arg_kind_name(k); }

// An argument that remembers whether the callee moved from it.
struct ArgProbe {
    int value = 0;
    bool moved_from = false;
};

// Element constructible ONLY from explicit multi-argument constructors (no
// default ctor). Every MultiArg constructor counts as value_ctor.
//   MultiArg(int a, int b)                  value() == a, second() == b
//   MultiArg(int a, ArgProbe&)              arg_kind() == lvalue
//   MultiArg(int a, const ArgProbe&)        arg_kind() == const_lvalue
//   MultiArg(int a, ArgProbe&&)             arg_kind() == rvalue, marks the probe moved_from
//   MultiArg(std::unique_ptr<int>)          move-only argument; value() == *p
// emplace_back(1, probe) must produce lvalue; emplace_back(1, std::move(probe))
// must produce rvalue; anything else means the arguments were not forwarded
// with std::forward<Args>(args)...
class MultiArg : public BasicTracked<kMultiArg> {
    using Base = BasicTracked<kMultiArg>;

public:
    explicit MultiArg(int a, int b) : Base(a), second_(b) {}
    explicit MultiArg(int a, ArgProbe& p) : Base(a), second_(p.value), kind_(ArgKind::lvalue) {}
    explicit MultiArg(int a, const ArgProbe& p)
        : Base(a), second_(p.value), kind_(ArgKind::const_lvalue) {}
    explicit MultiArg(int a, ArgProbe&& p) : Base(a), second_(p.value), kind_(ArgKind::rvalue) {
        p.moved_from = true;
    }
    explicit MultiArg(std::unique_ptr<int> p) : Base(p ? *p : 0), kind_(ArgKind::rvalue) {}

    int second() const noexcept { return second_; }
    ArgKind arg_kind() const noexcept { return kind_; }

private:
    int second_ = 0;
    ArgKind kind_ = ArgKind::none;
};

// ---------------------------------------------------------------------------
// Self-checks: the harness types have exactly the properties tests rely on.
// ---------------------------------------------------------------------------
static_assert(std::is_default_constructible_v<Tracked>);
static_assert(std::is_nothrow_move_constructible_v<Tracked>);
static_assert(std::is_copy_constructible_v<Tracked>);
static_assert(!std::is_constructible_v<Tracked, double*>);
static_assert(!std::is_convertible_v<int, Tracked>, "Tracked(int) is explicit");

static_assert(std::is_move_constructible_v<MayThrowMove>);
static_assert(!std::is_nothrow_move_constructible_v<MayThrowMove>);
static_assert(std::is_copy_constructible_v<MayThrowMove>);

static_assert(std::is_copy_constructible_v<CopyOnly>);
static_assert(std::is_move_constructible_v<CopyOnly>, "rvalues bind to the copy constructor");
static_assert(!std::is_nothrow_move_constructible_v<CopyOnly>);

static_assert(!std::is_copy_constructible_v<MoveOnly>);
static_assert(!std::is_copy_assignable_v<MoveOnly>);
static_assert(std::is_nothrow_move_constructible_v<MoveOnly>);

static_assert(!std::is_copy_constructible_v<MoveOnlyMayThrow>);
static_assert(std::is_move_constructible_v<MoveOnlyMayThrow>);
static_assert(!std::is_nothrow_move_constructible_v<MoveOnlyMayThrow>);

static_assert(!std::is_default_constructible_v<NoDefault>);
static_assert(std::is_nothrow_move_constructible_v<NoDefault>);

static_assert(!std::is_default_constructible_v<MultiArg>);
static_assert(!std::is_convertible_v<int, MultiArg>);
static_assert(std::is_nothrow_move_constructible_v<MultiArg>);

}  // namespace cppatlas
