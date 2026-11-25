// tests/support/trace.hpp - optional JSON Lines event trace for the tester.
//
// OFF BY DEFAULT. Enable at run time with an environment variable:
//
//     CPPATLAS_TRACE=trace.jsonl ./build/debug/stage_02     # write to a file
//     CPPATLAS_TRACE=- ./build/debug/stage_02               # write to stderr
//
// Every instrumented element (tracked.hpp) then emits one JSON object per line:
//
//     {"ev":"test","name":"push_back appends one element"}
//     {"ev":"copy_construct","type":"Tracked","id":7,"from":3,"addr":"0x6020000001d0","value":42}
//     {"ev":"destroy","type":"Tracked","id":7,"addr":"0x6020000001d0","value":42}
//     {"ev":"error","what":"bad_destroy","detail":"..."}
//     {"ev":"note","text":"anything a test author passes to trace::note()"}
//
// "ev" is one of: test, note, error, injected_throw, and the cppatlas::Op names
// (default_construct, value_construct, copy_construct, move_construct,
// copy_assign, move_assign, destroy). "from" is the id of the source object for
// copies/moves. The event kinds "alloc" and "dealloc" are reserved (not emitted
// today; the replaced global operator new of alloc_check.hpp only counts).
//
// Cost when off: one predictable branch on a cached bool per element event.
// Compile it out completely with -DCPPATLAS_NO_TRACE.
#pragma once

#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

namespace cppatlas::trace {

#if defined(CPPATLAS_NO_TRACE)

inline bool enabled() noexcept { return false; }
inline void element(const char*, const char*, long, const void*, int, long = 0) noexcept {}
inline void test_begin(std::string_view) noexcept {}
inline void note(std::string_view) noexcept {}
inline void error(std::string_view, std::string_view) noexcept {}

#else

namespace detail {

struct Sink {
    std::FILE* file = nullptr;
    bool owned = false;

    Sink() noexcept {
        const char* path = std::getenv("CPPATLAS_TRACE");
        if (path == nullptr || *path == '\0') return;
        if (std::string_view(path) == "-") {
            file = stderr;
            return;
        }
        file = std::fopen(path, "w");
        owned = file != nullptr;
    }
    ~Sink() {
        if (owned) std::fclose(file);
    }
    Sink(const Sink&) = delete;
    Sink& operator=(const Sink&) = delete;
};

inline Sink& sink() noexcept {
    static Sink s;
    return s;
}

inline void write_json_string(std::FILE* f, std::string_view s) noexcept {
    std::fputc('"', f);
    for (char c : s) {
        switch (c) {
        case '"': std::fputs("\\\"", f); break;
        case '\\': std::fputs("\\\\", f); break;
        case '\n': std::fputs("\\n", f); break;
        case '\t': std::fputs("\\t", f); break;
        default:
            if (static_cast<unsigned char>(c) < 0x20)
                std::fprintf(f, "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
            else
                std::fputc(c, f);
        }
    }
    std::fputc('"', f);
}

}  // namespace detail

// True when CPPATLAS_TRACE is set (checked once per process).
inline bool enabled() noexcept { return detail::sink().file != nullptr; }

// One element lifetime event. `ev` is an Op name, `type` the element type name.
inline void element(const char* ev, const char* type, long id, const void* addr, int value,
                    long from_id = 0) noexcept {
    if (!enabled()) return;
    std::FILE* f = detail::sink().file;
    std::fprintf(f, "{\"ev\":\"%s\",\"type\":\"%s\",\"id\":%ld,", ev, type, id);
    if (from_id != 0) std::fprintf(f, "\"from\":%ld,", from_id);
    std::fprintf(f, "\"addr\":\"%p\",\"value\":%d}\n", addr, value);
}

// Marks the start of a test case (called by CPPATLAS_TEST automatically).
inline void test_begin(std::string_view name) noexcept {
    if (!enabled()) return;
    std::FILE* f = detail::sink().file;
    std::fputs("{\"ev\":\"test\",\"name\":", f);
    detail::write_json_string(f, name);
    std::fputs("}\n", f);
    std::fflush(f);
}

// Free-form marker a test can drop into the trace ("before reserve(10)").
inline void note(std::string_view text) noexcept {
    if (!enabled()) return;
    std::FILE* f = detail::sink().file;
    std::fputs("{\"ev\":\"note\",\"text\":", f);
    detail::write_json_string(f, text);
    std::fputs("}\n", f);
}

// A lifetime error detected by the instrumented types (tracked.hpp).
inline void error(std::string_view what, std::string_view detail_text) noexcept {
    if (!enabled()) return;
    std::FILE* f = detail::sink().file;
    std::fputs("{\"ev\":\"error\",\"what\":", f);
    detail::write_json_string(f, what);
    std::fputs(",\"detail\":", f);
    detail::write_json_string(f, detail_text);
    std::fputs("}\n", f);
    std::fflush(f);
}

#endif  // CPPATLAS_NO_TRACE

}  // namespace cppatlas::trace
