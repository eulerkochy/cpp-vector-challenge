// tests/support/doctest_main.cpp - the single doctest implementation + main().
// Compiled ONCE into the `cppatlas_doctest_main` library that every stage
// executable links, so editing my/vector.hpp only recompiles stage TUs.
//
// Also home of the allocation check (alloc_check.hpp): the replacement global
// operator new / delete live in THIS translation unit on purpose. It defines
// main(), so the linker always pulls this object out of the static library -
// and the replacement operators with it.
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"

#include <cstdio>
#include <cstdlib>
#include <new>
#include <sstream>
#include <string>

#include "alloc_check.hpp"

#if CPPATLAS_ALLOC_CHECK_COMPILED

namespace {

void* checked_alloc(std::size_t n, std::size_t align, bool nothrow) {
    if (n == 0) n = 1;
    for (;;) {
        void* p = nullptr;
        if (align <= alignof(std::max_align_t)) {
            p = std::malloc(n);
        } else if (::posix_memalign(&p, align < sizeof(void*) ? sizeof(void*) : align, n) != 0) {
            p = nullptr;
        }
        if (p != nullptr) {
            cppatlas::alloc_check::detail::on_alloc(p, n);
            return p;
        }
        std::new_handler handler = std::get_new_handler();
        if (handler == nullptr) {
            if (nothrow) return nullptr;
            throw std::bad_alloc();
        }
        if (nothrow) {
            try {
                handler();
            } catch (...) {
                return nullptr;
            }
        } else {
            handler();
        }
    }
}

void checked_free(void* p) noexcept {
    if (p == nullptr) return;
    cppatlas::alloc_check::detail::on_free(p);
    std::free(p);  // malloc and posix_memalign memory are both released by free()
}

}  // namespace

// The replaceable global allocation functions ([new.delete]), all of them, so
// that no variant falls through to the library's version.
void* operator new(std::size_t n) { return checked_alloc(n, 0, false); }
void* operator new[](std::size_t n) { return checked_alloc(n, 0, false); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { return checked_alloc(n, 0, true); }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { return checked_alloc(n, 0, true); }
void* operator new(std::size_t n, std::align_val_t a) { return checked_alloc(n, static_cast<std::size_t>(a), false); }
void* operator new[](std::size_t n, std::align_val_t a) {
    return checked_alloc(n, static_cast<std::size_t>(a), false);
}
void* operator new(std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept {
    return checked_alloc(n, static_cast<std::size_t>(a), true);
}
void* operator new[](std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept {
    return checked_alloc(n, static_cast<std::size_t>(a), true);
}

void operator delete(void* p) noexcept { checked_free(p); }
void operator delete[](void* p) noexcept { checked_free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { checked_free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { checked_free(p); }
void operator delete(void* p, std::size_t) noexcept { checked_free(p); }
void operator delete[](void* p, std::size_t) noexcept { checked_free(p); }
void operator delete(void* p, std::align_val_t) noexcept { checked_free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { checked_free(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { checked_free(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { checked_free(p); }
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept { checked_free(p); }
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept { checked_free(p); }

#endif  // CPPATLAS_ALLOC_CHECK_COMPILED

namespace {

// Counts failed assertions per test case, so the allocation check reports only
// on tests that otherwise passed (alloc_check::failures_in_current_test()).
struct FailureCounter : doctest::IReporter {
    explicit FailureCounter(const doctest::ContextOptions&) {}
    void report_query(const doctest::QueryData&) override {}
    void test_run_start() override {}
    void test_run_end(const doctest::TestRunStats&) override {}
    void test_case_start(const doctest::TestCaseData&) override { reset(); }
    void test_case_reenter(const doctest::TestCaseData&) override { reset(); }
    void test_case_end(const doctest::CurrentTestCaseStats&) override {}
    void test_case_exception(const doctest::TestCaseException&) override {}
    void subcase_start(const doctest::SubcaseSignature&) override {}
    void subcase_end() override {}
    void log_assert(const doctest::AssertData& ad) override {
        if (ad.m_failed && (ad.m_at & doctest::assertType::is_warn) == 0) ++state().failures;
    }
    void log_message(const doctest::MessageData& md) override {
        if ((md.m_severity & doctest::assertType::is_warn) == 0) ++state().failures;
    }
    void test_case_skipped(const doctest::TestCaseData&) override {}

private:
    static cppatlas::alloc_check::detail::State& state() { return cppatlas::alloc_check::detail::state(); }
    static void reset() { state().failures = 0; }
};

// One-time lazy allocations that would otherwise land in the first test that
// triggers them: libstdc++'s numeric-formatting caches of the global locale,
// and doctest's INFO / CAPTURE context stack (a vector that only grows).
void nest_info(int depth) {
    INFO("warm-up " << depth);
    if (depth > 0) nest_info(depth - 1);
}

void warm_up() {
    std::ostringstream os;
    os << 1 << ' ' << -1L << ' ' << 1UL << ' ' << 1LL << ' ' << 1.5 << ' ' << 1.5f << ' ' << true << ' '
       << static_cast<const void*>(&os) << ' ' << 'c' << ' ' << std::string(64, 'x') << std::boolalpha << false;
    (void)os.str();
    std::wostringstream wos;
    wos << 1 << L' ' << 1.5;
    (void)wos.str();
    (void)std::to_string(42);
    nest_info(32);
}

}  // namespace

DOCTEST_REGISTER_LISTENER("cppatlas_failure_counter", 1, FailureCounter);

int main(int argc, char** argv) {
    // Line-buffer stdout: if a sanitizer or a crash aborts the process, the
    // diagnoses printed by earlier test cases must already be on screen.
    std::setvbuf(stdout, nullptr, _IOLBF, BUFSIZ);
#if CPPATLAS_ALLOC_CHECK_COMPILED
    {
        const char* off = std::getenv("CPPATLAS_NO_ALLOC_CHECK");
        cppatlas::alloc_check::detail::state().enabled = off == nullptr || *off == '\0' || *off == '0';
    }
#endif
    warm_up();
    doctest::Context context;
    // Defaults tuned for learners; any of them can be overridden on the
    // command line (e.g. ./stage_02 --no-path-filenames=false -s).
    context.setOption("no-intro", true);        // no "[doctest] doctest version is ..." banner
    context.setOption("no-path-filenames", true);  // stage_02.cpp:41 instead of /long/path/...
    context.setOption("order-by", "file");      // tests run top-to-bottom, simplest first
    context.applyCommandLine(argc, argv);
    return context.run();
}
