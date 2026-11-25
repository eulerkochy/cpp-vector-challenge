// tests/support/subject.hpp - the ONE place that decides what `my::vector` is.
//
//   default              #include <my/vector.hpp>   (the learner's header)
//   -DUSE_STD=ON         CMake defines CPPATLAS_USE_STD and my::vector<T> becomes
//                        an alias for std::vector<T>.
//
// USE_STD exists to keep the tests honest: every test must pass against
// std::vector on both libstdc++ and libc++, so tests may only assert what the
// standard guarantees (no exact growth factor, no capacity()==0 after default
// construction, no particular moved-from state of std types, ...).
//
// Tests must only ever spell the type as `my::vector<T>` with ONE template
// argument (the learner is not required to support an allocator parameter).
#pragma once

#if defined(CPPATLAS_USE_STD)

#include <vector>

namespace my {
template <class T>
using vector = std::vector<T>;
}  // namespace my

namespace cppatlas {
inline constexpr bool testing_std_vector = true;
}

#else

#include <my/vector.hpp>

namespace cppatlas {
inline constexpr bool testing_std_vector = false;
}

#endif
