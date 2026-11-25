// tests/support/selftest.cpp - tests for the HARNESS itself (not a stage).
//
// Always built against std::vector (CPPATLAS_USE_STD is forced for this
// target), so it never depends on the learner's code. Target
// `harness_selftest`, ctest name `harness-selftest`, label `harness`.
// It doubles as executable documentation of the support API for stage authors.
#include "harness.hpp"

#include <cstdint>
#include <memory>
#include <new>
#include <string>
#include <vector>

using namespace cppatlas;

namespace preflight {
CPPATLAS_PREFLIGHT(has_reserve, "selftest: std::vector has reserve", my::vector<Tracked>,
                   requires(V& v) { v.reserve(1); });
}
CPPATLAS_STAGE_GATE(preflight::has_reserve);

static_assert(testing_std_vector, "the self-test must be built with CPPATLAS_USE_STD");

CPPATLAS_TEST("counters: CounterScope deltas and live level") {
    CounterScope scope;
    {
        Tracked a(1);
        Tracked b = a;             // copy
        Tracked c = std::move(a);  // move
        b = c;                     // copy-assign
        c = std::move(b);          // move-assign
        CHECK(a.moved_from());
        CHECK_FALSE(a == 1);
        CHECK(c == 1);
        CHECK(counts().live - scope.start().live == 3);
    }
    const Counts d = scope.delta();
    CHECK(d.value_ctor == 1);
    CHECK(d.copy_ctor == 1);
    CHECK(d.move_ctor == 1);
    CHECK(d.copy_assign == 1);
    CHECK(d.move_assign == 1);
    CHECK(d.dtor == 3);
    CHECK(d.live == 0);
    CHECK(d.copies() == 2);
    CHECK(d.moves() == 2);
    CHECK(d.element_ops() == 8);
    VERIFY(expect::destroyed_exactly_once(d));
}

CPPATLAS_TEST("reserve relocates nothrow-movable elements by move") {
    vector<Tracked> v;
    v.reserve(3);
    for (int i = 0; i < 3; ++i) v.push_back(Tracked(i));
    CounterScope scope;
    v.reserve(v.capacity() + 10);
    const Counts d = scope.delta();
    VERIFY(expect::relocated_by_move(d, 3));
    CHECK(d.dtor == 3);
    CHECK(v[0] == 0);
    CHECK(v[2] == 2);
}

CPPATLAS_TEST("reserve relocates may-throw-move and copy-only elements by copy") {
    vector<MayThrowMove> a;
    vector<CopyOnly> b;
    a.reserve(2);
    b.reserve(2);
    for (int i = 0; i < 2; ++i) {
        a.push_back(MayThrowMove(i));
        b.push_back(CopyOnly(i));
    }
    CounterScope scope;
    a.reserve(a.capacity() + 10);
    VERIFY(expect::relocated_by_copy(scope.delta(), 2));
    scope.restart();
    b.reserve(b.capacity() + 10);
    VERIFY(expect::relocated_by_copy(scope.delta(), 2));
}

CPPATLAS_TEST("ThrowOn: injected copy failure, strong guarantee of std::vector::reserve") {
    vector<MayThrowMove> v;
    v.reserve(3);
    for (int i = 0; i < 3; ++i) v.push_back(MayThrowMove(10 + i));
    const auto old_cap = v.capacity();
    {
        ThrowOn arm(Op::copy_construct, 2);  // the 2nd copy during relocation throws
        CHECK_THROWS_AS(v.reserve(old_cap + 10), InjectedError);
        CHECK(arm.fired());
    }
    CHECK(counts().injected == 1);
    REQUIRE(v.size() == 3);
    CHECK(v.capacity() == old_cap);
    CHECK(v[0] == 10);
    CHECK(v[1] == 11);
    CHECK(v[2] == 12);
    // disarmed: this does not throw
    v.reserve(old_cap + 10);
    CHECK(v.capacity() >= old_cap + 10);
}

CPPATLAS_TEST("ThrowOn: move injection fires for MayThrowMove, is ignored for noexcept moves") {
    {
        ThrowOn arm(Op::move_construct, 1);
        MayThrowMove a(1);
        CHECK_THROWS_AS(MayThrowMove(std::move(a)), InjectedError);
        CHECK(arm.fired());
    }
    {
        ThrowOn arm(Op::move_construct, 1);
        Tracked a(1);
        Tracked b(std::move(a));  // noexcept: never throws
        CHECK_FALSE(arm.fired());
        CHECK(b == 1);
    }
}

CPPATLAS_TEST("MoveOnly elements") {
    vector<MoveOnly> v;
    MoveOnly m(7);
    v.push_back(std::move(m));
    CHECK(m.moved_from());
    REQUIRE(v.size() == 1);
    CHECK(v[0] == 7);
}

CPPATLAS_TEST("MultiArg records how arguments were forwarded") {
    vector<MultiArg> v;
    v.reserve(8);
    ArgProbe p{5};
    const ArgProbe cp{6};
    CounterScope scope;
    v.emplace_back(1, 2);
    v.emplace_back(1, p);
    v.emplace_back(1, cp);
    v.emplace_back(1, std::move(p));
    v.emplace_back(std::make_unique<int>(9));
    const Counts d = scope.delta();
    REQUIRE(v.size() == 5);
    CHECK(d.value_ctor == 5);
    CHECK(d.copies() == 0);
    CHECK(d.moves() == 0);
    CHECK(v[0].second() == 2);
    CHECK(v[1].arg_kind() == ArgKind::lvalue);
    CHECK(v[2].arg_kind() == ArgKind::const_lvalue);
    CHECK(v[3].arg_kind() == ArgKind::rvalue);
    CHECK(p.moved_from);
    CHECK(v[4] == 9);
}

CPPATLAS_TEST("lifetime errors are detected and diagnosed") {
    alignas(Tracked) unsigned char raw[2 * sizeof(Tracked)] = {};
    auto* slot = reinterpret_cast<Tracked*>(raw);

    std::construct_at(slot, 3);
    std::destroy_at(slot);
    std::destroy_at(slot);  // double destroy
    CHECK(counts().bad_destroy == 1);
    Verdict v = expect::lifetime_clean();
    CHECK_FALSE(v.ok);
    CHECK(v.message.find("destroyed") != std::string::npos);

    Tracked src(4);
    slot[1] = src;  // assignment into raw memory
    CHECK(counts().assign_to_raw == 1);
    std::destroy_at(slot + 1);

    clear_lifetime_errors();  // deliberate errors; do not fail the automatic checks
}

CPPATLAS_TEST("verdict messages diagnose the usual mistakes") {
    Counts copied;
    copied.copy_ctor = 3;
    copied.dtor = 3;
    const Verdict a = expect::relocated_by_move(copied, 3);
    CHECK_FALSE(a.ok);
    CHECK(a.message.find("move_if_noexcept") != std::string::npos);

    Counts moved;
    moved.move_ctor = 3;
    const Verdict b = expect::relocated_by_copy(moved, 3);
    CHECK_FALSE(b.ok);
    CHECK(b.message.find("noexcept") != std::string::npos);

    Counts defaulted;
    defaulted.default_ctor = 4;
    const Verdict c = expect::no_default_constructions(defaulted);
    CHECK_FALSE(c.ok);
    CHECK(c.message.find("new T[n]") != std::string::npos);

    CHECK(expect::ops(moved, Op::move_construct, 3).ok);
    CHECK_FALSE(expect::no_element_ops(moved, "moving the vector").ok);
}

CPPATLAS_TEST("MoveOnlyMayThrow: moved by std::vector relocation, move injection fires") {
    vector<MoveOnlyMayThrow> v;
    for (int i = 0; i < 4; ++i) v.emplace_back(i);
    CounterScope scope;
    v.reserve(v.capacity() + 10);
    VERIFY(expect::relocated_by_move(scope.delta(), 4));
    {
        ThrowOn arm(Op::move_construct, 1);
        MoveOnlyMayThrow a(1);
        CHECK_THROWS_AS(MoveOnlyMayThrow(std::move(a)), InjectedError);
        CHECK(arm.fired());
    }
}

CPPATLAS_TEST("expect::relocated / no_copies / at_most") {
    vector<Tracked> v;
    for (int i = 0; i < 3; ++i) v.emplace_back(i);
    CounterScope scope;
    v.reserve(v.capacity() + 10);
    CHECK(expect::relocated(scope.delta(), 3).ok);
    CHECK(expect::no_copies(scope.delta(), "reserve").ok);

    Counts leaked;
    leaked.copy_ctor = 3;
    leaked.dtor = 1;
    const Verdict a = expect::relocated(leaked, 3);
    CHECK_FALSE(a.ok);
    CHECK(a.message.find("not all destroyed") != std::string::npos);
    Counts assigned;
    assigned.copy_assign = 3;
    assigned.dtor = 3;
    CHECK(expect::relocated(assigned, 3).message.find("ASSIGNED") != std::string::npos);
    CHECK_FALSE(expect::no_copies(leaked, "x").ok);
    CHECK(expect::at_most(2, 2, "n").ok);
    CHECK_FALSE(expect::at_most(3, 2, "n").ok);
}

CPPATLAS_TEST("Snapshot + expect::unchanged detect any observable change") {
    vector<Tracked> v;
    v.reserve(8);
    for (int i = 0; i < 4; ++i) v.emplace_back(i);
    const Snapshot s = snapshot(v);
    CHECK(s.size == 4);
    CHECK(s.first == static_cast<const void*>(v.data()));
    CHECK(expect::unchanged(s, v, "nothing").ok);

    v[2] = Tracked(42);  // same object (id), different value
    const Verdict changed_value = expect::unchanged(s, v, "assignment");
    CHECK_FALSE(changed_value.ok);
    CHECK(changed_value.message.find("v[2]") != std::string::npos);
    v[2] = Tracked(2);
    CHECK(expect::unchanged(s, v, "restored value").ok);

    vector<Tracked> copy = v;  // same values, different objects and buffer
    CHECK_FALSE(expect::unchanged(s, copy, "copy").ok);

    v.emplace_back(4);
    CHECK(expect::unchanged(s, v, "push").message.find("size()") != std::string::npos);
    v.pop_back();
    v.reserve(100);  // same values, but relocated
    CHECK_FALSE(expect::unchanged(s, v, "reserve").ok);
}

// Plain TEST_CASE, not CPPATLAS_TEST: CPPATLAS_TEST already opens an
// alloc_check::Scope around the body, and scopes do not nest.
TEST_CASE("alloc_check: records allocations made in a scope until they are freed") {
    if (!alloc_check::enabled()) {
        MESSAGE("allocation check disabled (CPPATLAS_NO_ALLOC_CHECK or not compiled): skipped");
        return;
    }
    void* before = ::operator new(16);  // allocated outside the scope
    {
        alloc_check::Scope s;
        CHECK(s.reliable());
        CHECK(s.live() == 0);

        void* p = ::operator new(24);
        CHECK(s.live() == 1);
        CHECK(s.live_bytes() == 24);
        ::operator delete(p, 24);  // sized delete
        CHECK(s.live() == 0);

        void* a = ::operator new(128, std::align_val_t{64});
        CHECK(reinterpret_cast<std::uintptr_t>(a) % 64 == 0);
        int* arr = new int[10];
        void* nt = ::operator new(8, std::nothrow);
        CHECK(s.live() == 3);
        ::operator delete(a, std::align_val_t{64});
        delete[] arr;
        ::operator delete(nt, std::nothrow);
        CHECK(s.live() == 0);

        std::allocator<long> alloc;  // what most vectors use
        long* q = alloc.allocate(10);
        CHECK(s.live() == 1);
        CHECK(s.live_bytes() == 10 * sizeof(long));
        alloc.deallocate(q, 10);

        ::operator delete(before);  // allocated before the scope: ignored, never negative
        CHECK(s.live() == 0);

        void* leak = ::operator new(40);
        CHECK(s.live() == 1);
        s.stop();
        ::operator delete(leak);  // freed after stop(): still counted as leaked by the scope
        CHECK(s.live() == 1);
    }
    CHECK(alloc_check::failures_in_current_test() == 0);
}

TEST_CASE("alloc_check: allocations_freed explains a leak") {
    CHECK(expect::allocations_freed(0, 0).ok);
    const Verdict v = expect::allocations_freed(2, 96);
    CHECK_FALSE(v.ok);
    CHECK(v.message.find("2 allocation(s) (96 bytes)") != std::string::npos);
    CHECK(v.message.find("free the old buffer") != std::string::npos);
}

CPPATLAS_TEST("alloc_check: a std::vector test leaves nothing allocated (checked automatically)") {
    vector<std::string> v;
    for (int i = 0; i < 50; ++i) v.push_back(std::string(40, 'x'));
    v.reserve(1000);
    CHECK(v.size() == 50);
}

CPPATLAS_TEST("expect::first_failure reports the first failing verdict") {
    CHECK(expect::first_failure({Verdict::pass(), Verdict::pass()}).ok);
    const Verdict v = expect::first_failure({Verdict::pass(), Verdict::fail("first"), Verdict::fail("second")});
    CHECK_FALSE(v.ok);
    CHECK(v.message == "first");
}
