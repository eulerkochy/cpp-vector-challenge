// my/vector.hpp - YOUR std::vector. This is the only file you need to edit.
//
// Get graded with `./cppatlas submit`. It sends this file to the cppatlas
// grader (no local compiler needed), prints a [tester::#N] log for the stage
// you are on, and advances you when it passes. Each stage's spec lives at
// https://cppatlas.com/build/vector/<stage-slug>.
// Optional: `./cppatlas test` builds and runs the same tests locally, which is
// faster if you have a C++23 compiler and CMake installed.
//
// STAGE 1 (free pass, proves the submit loop works):
//   1) Uncomment the 5 lines marked with "STAGE 1" below
//      (3 return statements and 2 data members).
//   2) Run `./cppatlas submit`.
//   3) It passes and advances you to stage 2.
//   The placeholder lines next to them can stay or be deleted; they become dead
//   code once the marked lines are active.
//
// Everything else below COMPILES but is deliberately wrong, so the tests fail
// cleanly until you implement it.
//
//   Stage 1 (empty-vector): a default-constructed vector reports size() == 0,
//            capacity() >= size() and empty() == true.
//   Stage 2 (push-back):    raw storage + push_back(const T&) + operator[] +
//            a destructor that destroys the elements and frees the storage.
//
// Later stages (growth, copy/move, emplace_back, exception safety, iterators,
// insert/erase) need members that are not declared here yet. When a stage
// needs one, the grader will say exactly which one to add.
//
// Rules of the challenge:
//   - Do not include <vector> or wrap std::vector / std::deque / ...
//   - Keep the class a template with ONE type parameter: my::vector<T>.
#pragma once

#include <cstddef>    // std::size_t
#include <memory>     // std::allocator, std::construct_at, std::destroy_at (you will want these)
#include <new>        // placement new, ::operator new
#include <stdexcept>  // std::logic_error (used by the stubs below)
#include <utility>    // std::move, std::forward

namespace my {

template <class T>
class vector {
public:
    // ---------------------------------------------------------------- stage 1
    vector() noexcept = default;

    std::size_t size() const noexcept {
        // return size_;  // STAGE 1: uncomment this line
        return static_cast<std::size_t>(-1);  // placeholder: how many elements are stored?
    }

    std::size_t capacity() const noexcept {
        // return capacity_;  // STAGE 1: uncomment this line
        return 0;  // placeholder: how many elements fit before we must reallocate?
    }

    bool empty() const noexcept {
        // return size_ == 0;  // STAGE 1: uncomment this line
        return false;  // placeholder
    }

    // ---------------------------------------------------------------- stage 2
    ~vector() {
        // TODO(stage 2): destroy the elements (each exactly once), then free the storage.
    }

    void push_back(const T& value) {
        // TODO(stage 2): if there is room, construct a copy of `value` in the first
        // free slot. If not: allocate a bigger RAW buffer (do not default-construct
        // T!), construct the copy of `value` in it FIRST (`value` may be one of
        // your own elements), then move-construct the old elements into the new
        // buffer (stage 6 refines this to std::move_if_noexcept), destroy the old
        // elements and free the old buffer.
        (void)value;
    }

    T& operator[](std::size_t index) {
        (void)index;
        throw std::logic_error("my::vector::operator[] is not implemented yet (stage 2)");
    }

    const T& operator[](std::size_t index) const {
        (void)index;
        throw std::logic_error("my::vector::operator[] const is not implemented yet (stage 2)");
    }

    // ---------------------------------------------------------------- stage 4
    // Copying/moving a vector is stage 4. Until then they are disabled, so the
    // compiler-generated versions cannot silently share (and double-free) a buffer.
    vector(const vector&) = delete;
    vector& operator=(const vector&) = delete;

private:
    // std::size_t size_ = 0;       // STAGE 1: uncomment this line (constructed elements)
    // std::size_t capacity_ = 0;   // STAGE 1: uncomment this line (allocated slots)

    // Stage 2 will also need a pointer to the buffer. Add it when you need it:
    // T* data_ = nullptr;          // first element (raw storage from an allocator)
};

}  // namespace my
