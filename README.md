# Build your own `std::vector`

Eight test-driven stages that take you from an empty class to a vector with
geometric growth, copy/move, `emplace_back`, the strong exception guarantee,
contiguous iterators and `insert`/`erase`. You write `my::vector<T>` in one
header; a doctest suite tells you what to fix next.

Guide and stage pages: <https://cppatlas.com/build/vector>. Grading is remote by
default (`./cppatlas submit`); running the tests locally is optional.

You edit exactly one file: **`include/my/vector.hpp`**. Everything else is the
harness. If you get stuck, the reference solution is public at
<https://github.com/eulerkochy/cpp-atlas/tree/main/challenges/vector/solution>.
Try each stage on your own first: the stage page's hints and the failure
messages are meant to get you there.

## Stages

| # | Stage | Spec |
|---|-------|------|
| 1 | Empty vector | <https://cppatlas.com/build/vector/empty-vector> |
| 2 | push_back and storage | <https://cppatlas.com/build/vector/push-back> |
| 3 | Reserve and geometric growth | <https://cppatlas.com/build/vector/growth> |
| 4 | Copy and move | <https://cppatlas.com/build/vector/copy-and-move> |
| 5 | emplace_back, pop_back, clear | <https://cppatlas.com/build/vector/emplace-back> |
| 6 | Strong exception safety | <https://cppatlas.com/build/vector/exception-safety> |
| 7 | Iterators and member types | <https://cppatlas.com/build/vector/iterators> |
| 8 | insert and erase | <https://cppatlas.com/build/vector/insert-erase> |

`./cppatlas stages` prints the same list with links.

## Quick start

1. **Use this template.** Click **Use this template** on
   <https://github.com/eulerkochy/cpp-vector-challenge>
   (or go to
   <https://github.com/new?template_name=cpp-vector-challenge&template_owner=eulerkochy>)
   to create your own copy.
2. **Clone it** (or open it in a Codespace).
3. **Run `./cppatlas submit`.** For stage 1, first uncomment the marked lines in
   `include/my/vector.hpp` (see
   <https://cppatlas.com/build/vector/empty-vector>). The command sends your
   header to the grader at <https://run.cppatlas.com>, prints a live
   `[tester::#N]` log, and when the stage passes advances `.cppatlas.json` to
   the next stage. Commit that file.
4. **Install the GitHub App.** Go to
   <https://github.com/apps/c-atlas-grader/installations/new>. Choosing "All
   repositories" is safe: repositories without a `.cppatlas.json` are ignored.
   Or pick just this repository.
5. **Push.** From then on, `git push` also grades your commit and posts a check
   named `cppatlas / vector` on it.

`./cppatlas submit` needs only `curl` and `python3` or `jq`. No local compiler
is required. The grader uses GCC 16.2 (default) or Clang 21 in C++23 mode.

```
./cppatlas submit                      # stages 1..current; advances on pass
./cppatlas submit --stage 3            # stages 1..3, never advances
./cppatlas submit --compiler clang     # gcc (default) or clang
./cppatlas submit --sanitize           # AddressSanitizer + UndefinedBehaviorSanitizer
./cppatlas help                        # all commands
```

Prefer a manual run on GitHub instead? The repository also has a workflow:
Actions tab, then "tests", then "Run workflow". It does not run on push.

## The edit-submit loop

1. Read the stage page (start with stage 1).
2. Edit `include/my/vector.hpp`.
3. Run `./cppatlas submit`.

`./cppatlas submit` grades stages 1 through your **current stage** and **stops
at the first failing stage**, so earlier stages act as regression tests while
you work on later ones. The starter header compiles but is deliberately
incomplete; stage 1 fails until you uncomment its lines.

### Your progress: `.cppatlas.json`

The file `.cppatlas.json` at the repository root records which stage you are on:

```json
{"challenge":"vector","stage":1}
```

When stages 1..*stage* all pass, `./cppatlas submit` advances the file and tells you
which stage is next and where its page is.

**Commit the file after it advances** (`git add .cppatlas.json`). The GitHub App
reads the stage from that file at the pushed commit and grades stages
1..*stage*, so a push is green when your current stage passes. Explicit forms
(`--stage 3`, `./cppatlas test 3`, `--only 3`) never change the file. The file
must exist and `stage` must be 1 to 8, otherwise `./cppatlas` reports a clear
error. Once stage 8 passes you are done.

### Reading a failure

There are two kinds, and the log tells you which.

**A compile failure with a bullet list.** Each stage's tests check, before
compiling the test bodies, that your class has the members the stage needs. A
missing member is reported as one bullet instead of a wall of template errors:

```
stage 2 ✗ push-back (does not compile)

  Your vector is missing something this stage needs:
    - stage 2 (push-back): my::vector<T> needs `void push_back(const T& value)`.
```

Add exactly that member. If real compiler errors from your own code appear,
they are listed first ("fix these first"); they can be the reason a member
looks missing.

**A runtime diagnostic.** Your code compiled but a check failed. The message
names the test case, the line, the values, and a hint about what the check
means:

```
stage_01.cpp:69: ERROR: CHECK( v.size() == 0 ) is NOT correct!
  values: CHECK( 18446744073709551615 == 0 )
  logged: a vector that was just created holds no elements, so size() must return 0
```

The tests also count constructions, copies, moves and destructions of
instrumented element types, so a leak, a double destroy or an unnecessary copy
is reported as a failure with the counts. A crash prints the stage and test
case and suggests the sanitizer run. The last line links to the spec page for
the failing stage.

### Sanitizers

```
./cppatlas submit --sanitize
```

`--sanitize` grades with AddressSanitizer and UndefinedBehaviorSanitizer. Use
it from stage 2 onward and whenever a stage crashes: a double free, a leak, an
out-of-bounds access, a read of a destroyed element or a misaligned object
becomes an immediate, precisely located report instead of a mystery crash three
tests later. It is slower, so use the plain run for the quick loop.

## Optional: run the tests locally

Remote grading is the default. Running the suite on your own machine is a
faster loop and lets you use a debugger, but it needs a toolchain
(see [Requirements](#requirements)). The commands below are only for local
testing.

```
./cppatlas doctor            # checks cmake, ninja, compilers, C++23 and sanitizer probes
./cppatlas test              # stages 1..current stage; advances when they pass
./cppatlas test --no-advance # same, but leave .cppatlas.json alone
./cppatlas test 3            # stages 1..3 (never advances)
./cppatlas test --only 3     # just stage 3 (never advances)
./cppatlas test -v           # full compiler / test output
./cppatlas test --preset asan-ubsan
./cppatlas stages            # list stages and spec links
./cppatlas clean             # delete build/
```

`./cppatlas doctor` compares your tools with the grader's versions and prints
warnings. It never fails hard, and you do not need it for `submit`. A stage
that runs longer than 30 s locally is killed and the test case it was in is
named (usually an infinite loop; change the limit with `CPPATLAS_TIMEOUT=60`).

### Option A: dev container / GitHub Codespaces (recommended)

Open the repository in a Codespace, or in VS Code run "Dev Containers: Reopen
in Container". You get GCC 16.2, Clang 21, CMake, Ninja, ccache, gdb and lldb,
with the sanitizers working. The first build is done for you.

```
./cppatlas test
```

Default compiler in the container is GCC 16.2 (`CXX=g++`). To use Clang for a
run: `CXX=clang++-21 ./cppatlas test`. Each compiler keeps its own build
directory, so switching is cheap. Both compilers use GCC 16.2's libstdc++, so
they see the same C++23 standard library.

### Option B: your own machine

You need bash, CMake, Ninja and a C++23 compiler (see [Requirements](#requirements)).

```
./cppatlas doctor
./cppatlas test
```

## Plain CMake and CTest (local)

`./cppatlas test` is a thin wrapper. The same thing by hand:

```
cmake --preset debug                  # or: asan-ubsan
cmake --build --preset debug
ctest --preset debug                  # all stages
ctest --preset debug -L stage-03      # one stage
```

Build directories are `build/<preset>`. Pick a compiler with
`CXX=clang++-21 cmake --preset debug` (use a fresh build directory when you
change it). Targets are `stage_01` ... `stage_08`; the `harness-selftest` test
checks the harness itself and does not depend on your code.

## Are the tests fair?

The same suite compiles and passes against `std::vector`:

```
CPPATLAS_CMAKE_ARGS="-DUSE_STD=ON" ./cppatlas test
```

(`--preset asan-ubsan` works too. This runs locally.) So every assertion checks behavior the
standard guarantees. If a test fails on your vector, your vector is wrong, not
merely different from libstdc++. Implementation choices such as the growth
factor or the exact capacity after `reserve` are deliberately not asserted.
`CPPATLAS_CMAKE_ARGS` is meant for this kind of check; leave it unset normally.

## Requirements

Only for local testing (`./cppatlas test`). `./cppatlas submit` needs just `curl` and `python3` or `jq`.

- bash (the `./cppatlas` script is bash 3.2 compatible)
- CMake 3.25 or newer (`CMakeLists.txt` requires 3.23; `CMakePresets.json` uses preset schema version 6, which needs 3.25)
- Ninja
- A C++23 compiler and standard library. The verified ones are **GCC 16.2** and **Clang 21**; older compilers are unsupported.
- Optional: ccache (used automatically if found)

What has actually been run: GCC 16.2 and Clang 21 (with GCC 16.2's libstdc++) in
the dev container (Debian 13), and Homebrew GCC 16.2 and LLVM 21 on macOS.

## Supported environments

| Tier | Environment | Status |
|------|-------------|--------|
| 1 | Dev container / Codespaces (Debian 13, GCC 16.2 default + Clang 21, ASan, UBSan, LSan) | Fully supported. Matches the grader's toolchain. |
| 2 | macOS with Homebrew GCC 16.2 or LLVM 21 | Supported. No LeakSanitizer on macOS (the harness's live-object counter still catches leaked elements). |
| 3 | Windows with MSVC | Best effort. Needs a Visual Studio release whose MSVC accepts `/std:c++latest` (C++23 preview) for this code; no UBSan; use the `debug` preset only. The dev container or WSL is recommended. |

macOS notes: Apple's bundled clang can lag in C++23 library support, so use
Homebrew LLVM (`CXX=/opt/homebrew/opt/llvm/bin/clang++ ./cppatlas test`) or
Homebrew GCC (`CXX=g++-16`). Some Homebrew LLVM releases (seen with 21.1.2 on
macOS 26/27) ship an AddressSanitizer runtime that hangs during start-up. The
build detects this at configure time and, if so, links Apple's sanitizer
runtime instead and prints a `WORKAROUND` line (see `cmake/Sanitizers.cmake`).
Nothing for you to do.

On Windows, `./cppatlas` needs a bash (Git Bash or WSL) and the
`debug` preset uses Ninja, so run it from a "Developer Command Prompt" shell
where `cl` is on `PATH`. This path is not routinely tested.

## Trace output (optional)

Set `CPPATLAS_TRACE` to a file path (or `-` for stderr) and run a stage
executable directly to get a JSON Lines log of every construct, copy, move and
destroy of the instrumented element types, with object ids and addresses:

```
CPPATLAS_TRACE=trace.jsonl ./build/debug/stage_03
```

Handy for seeing exactly which element was destroyed twice. Off by default;
`*.jsonl` is git-ignored.

## Grading on push, and the manual workflow

Installing the GitHub App (step 4 of the quick start) grades every `git push`
on repositories that have a `.cppatlas.json`. The app reads `.cppatlas.json` and
`include/my/vector.hpp` at the pushed commit, grades stages 1..*stage*, and
posts a check run named `cppatlas / vector` on the commit. Remember to commit
`.cppatlas.json` after it advances, otherwise the push is graded on the old
stage.

`.github/workflows/test.yml` is a manual fallback for people who do not install
the App. It runs only when you start it (Actions tab, then "tests", then "Run
workflow"), reads your stage from `.cppatlas.json`, and runs stages 1..*stage*
with `./cppatlas test <stage>` inside the dev container image, for GCC 16.2 and
Clang 21, with the `debug` and `asan-ubsan` presets. It writes a progress
summary on the run page.

## Troubleshooting

- **`cmake not found` / `ninja not found`**: install them (`brew install cmake ninja`, `apt install cmake ninja-build`), or use the dev container.
- **CMake configure fails with a preset or version error**: your CMake is older than 3.25. Upgrade it (`pip install cmake`, Homebrew, or the Kitware apt repo).
- **Errors that mention concepts, `<ranges>` or `std::construct_at` in code you did not write**: your compiler or standard library may be too old for C++23. Check `$CXX --version` against [Requirements](#requirements) and pick another with `CXX=...`.
- **A stage "did not finish within 30s"**: an infinite loop. The message names the test case. Typical cause: growing capacity as `capacity * 2` when capacity is 0.
- **Switched compiler or options and see odd results**: run `./cppatlas clean`. (Different `CXX` values already use separate build directories.)
- **ASan fails to start inside a container with a ptrace/permission error**: the container needs `--cap-add=SYS_PTRACE --security-opt seccomp=unconfined`; the provided devcontainer sets both.
- **Homebrew LLVM 21 on the macOS 27 SDK fails to compile `<cmath>` / `<random>` (`INFINITY` / `NAN` undefined)**: the SDK's `math.h` expects a `float.h` feature this LLVM build lacks. Use `g++-16` or the dev container for code that includes those headers.
- **Homebrew GCC on macOS cannot find `<stdio.h>`**: run `xcode-select --install` or set `SDKROOT=$(xcrun --show-sdk-path)`.

## License and third-party code

Your solution is yours. The test harness uses [doctest](https://github.com/doctest/doctest)
2.5.3 (MIT, Copyright (c) 2016-2023 Viktor Kirilov), vendored unmodified as
`tests/support/doctest.h`; its license is `tests/support/doctest.LICENSE.txt`
and the provenance is in `tests/support/THIRD_PARTY.md`.

