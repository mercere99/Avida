# CLAUDE.md

Operating rules for working in this codebase. Read all of it before making changes.

---

## Project facts

- **Language standard:** C++23. Prefer modern C++23 idioms (ranges, `constexpr`, `std::expected`, concepts, `[[nodiscard]]`, designated initializers, etc.) over older equivalents **when efficiency is comparable or better**. If a modern construct is cleaner but measurably slower on a hot path, keep the older form and leave a `// PERF:` comment explaining why.  If unsure, ask.
- **Target / toolchain:** Must compile and run under **Emscripten** (WebAssembly) as well as native clang and gcc. Don't introduce code that builds natively but breaks the Emscripten build. When a feature's portability across these is uncertain, flag it rather than assuming.
- **No exceptions.** The project is built `-fno-exceptions` for Emscripten performance. Do not add `throw`, `try`, or `catch`, and do not call standard library APIs in a way that relies on exceptions for control flow. For error handling use `std::expected` if run-time responses are reasonable, or `emp_assert` (my version of assert in the Empirical library) if it is programmer error. Treat "I'll just throw here" as a rule violation, not a shortcut.
- **Build commands:** Standard builds should have Makefiles.  In general, `make` should compile natively and `make web` should compile with Emscripten.
- **Directory layout:** All C++ files should be in `source/`, with native executables going in `build/` and Emscripten web output going in `web/`.
- **Dependencies:** This project uses the Empirical scientific software library.  Adding additional third-party dependencies is a decision that requires asking first, not a default move. Prefer the standard library or what's already vendored.

## Tools in the Empirical Library

The Empirical library is found in ../Empirical/ (the Makefile uses -I../Empirical/include/ in compilation).  It is a header-only library that employs C++23.  From the Empirical include directory, I will make frequent use of:

- `emp/assert.hpp` - Defined macro `emp_assert` which behaves as a normal assert, but works well with Emscripten and also allows additional arguments in the assert.  Literal strings are printed as messages.  Variables or expressions are shown followed by what they evaluate to.
- `emp/base/notify.hpp` - Provides dynamic error handling that can be overridden for any particular executable.  Use `emp::notify::Error(...)` and `emp::notify::Warning(...)` for error/warning reporting.  The word "Error:" or "Warning:" will print and then all arguments will be printed in sequence to the appropriate output method.  `Error` will also abort the run, while `Warning` will not.
- `emp/base/Ptr.hpp` - `emp::Ptr<T>` does auditable pointer handling; it acts like a raw pointer, but in debug mode will point out any mis-handling.  Do not use raw pointers or shared pointers (though `std::unique_ptr` is okay to use.)
- `emp/base/vector.hpp` - `emp::vector` is identical to `std::vector`, but is auditable in debug mode.
- `emp/Bits.hpp` - Defines `emp::BitVector`, `emp::BitSet`, and several other bit handling mechanisms, all with the same core interface. These are fast and easy to use.
- `emp/compiler/Lexer.hpp` - Allows for the dynamic definition of a Lexer (`emp::Lexer`) using regular expressions.
- `emp/config/SettingsManager.hpp` - Helps handle configuration variables, both through a config file and command-line argument processing.
- `emp/data/DataOutput.hpp` - Provides a simple way of managing output files; the programmer can supply lambdas that are called to fill out rows in an output CSV file.
- `emp/datastructs/RobinHoodMap.hpp` and `emp/datastructs/RobinHoodSet.hpp`- provide fast and efficient hash table implementations using Robin Hood hashing.
- `emp/geometry/` - A set of useful classes when working with 2D geometric problems. 
- `emp/io/CPPFile.hpp` - Provides tools when generating an output file that will contain C++ code.
- `emp/io/File.hpp` - Provides general tools for working with files where you want to load an entire file into memory to manipulate and possibly save again.
- `emp/math/combos.hpp` - Tools to step through all possible subset combinations of a given size.
- `emp/math/Distribution.hpp` - Tools to work with pre-defined distributions of values, often that you want to draw a random value from.
- `emp/math/Random.hpp` - A random number generator that is faster, more random, and easier to use than the standard library.
- `emp/math/Range.hpp` - The `emp::Range` class tracks the simple boundaries for a range of numbers.
- `emp/tools/GridSize.hpp` - Defines `emp::GridSize` and `emp::GridPos` to make working with grids positions more intuitive.
- `emp/tools/String.hpp` - The `emp::String` class derives from `std::string`, adding many new member functions to simplify string manipulation.
- `emp/tools/Timer.hpp` - A simple class, useful for profiling.  Provide the timer name as a template parameter and it will start tracking time on construction and stop on destruction, aggregating over all instances.
- `emp/web/` - This directory holds many tools for creating and modifying web pages.

---

## Rules

### 1. Don't assume. Don't hide confusion. Surface tradeoffs.

Proceed without asking on low-stakes, easily-reversible choices (local naming, obvious control flow). **Stop and ask** when a non-trivial choice affects public API shape, data structures, on-disk/serialized formats, dependencies, build configuration, or anything expensive to unwind later. When more than one reasonable design exists with tradeoffs, present those tradeoffs and your recommendation **before** implementing — don't silently pick one and move on. Mark unresolved points inline with `// QUESTION:` or `// ASSUMPTION:` rather than burying them in prose or omitting them.

### 2. Minimize code to solve the problem. No speculative generality.

Solve the problem in front of you, not an anticipated future one without user agreement. No abstraction layers, template parameters, virtual interfaces, or extension points that aren't exercised by current calling code. "Minimum" means minimum *scope and indirection*, **not** fewest lines or cleverest expression — clear, direct code that a student could read beats dense code that's shorter. Tests, asserts, and comments on confusion sections are important code.

### 3. Touch only what you must.

Don't refactor, rename, or reformat code outside the task's scope, even if it looks improvable — that pollutes the diff and the review. Keep changes minimal and reviewable. If you notice an unrelated bug or other real issue, **note it** (in your summary or a `// TODO:` / `// BUG:` comment) rather than either silently fixing it (scope creep) or silently ignoring it (negligence). Scope discipline is the goal, not indifference to defects.

### 4. Define success criteria. Loop until verified — then stop.

State the success criteria *before* implementing; if they're non-obvious, confirm them. "Verified" has a specific meaning here: the code **compiles without warnings under C++23 using the project's Makefile using the appropriate target**. If you are unsure of which target to use, ask.  If you've recently used a target and it includes the relevant edited file(s), you may assume it's okay to keep using.  If the criteria can't be met after a couple of honest attempts, **stop and report what's failing** with specifics. Do not weaken the criteria, delete or skip a failing test (if there are any), or thrash to manufacture a green result.

---

## Additional expectations

- **Performance posture.** This is performance-sensitive code targeting Wasm. Avoid gratuitous heap allocation, unnecessary copies, and hidden O(n²) in hot paths. Don't micro-optimize speculatively either (see rule 2) — if a change is performance-motivated, say so and, where feasible, justify it with a number rather than a hunch.
- **Comments explain *why*, not *what*.** The code says what it does. Comments should capture intent, invariants, non-obvious constraints, and the reasons behind tradeoffs — especially anything Emscripten- or perf-driven that would otherwise look arbitrary to a future reader.
- **Match the existing style.** Naming, header/include conventions, error handling pattern, and structure should look like the surrounding code, not like a generic example. When in doubt, find the nearest analogous code in the repo and follow it.
- **Try to keep lines to 100 chars or less.** Only allow longer lines if it will lead to much clearer code.
- **Don't invent APIs or facts.** If you're unsure whether a standard library feature, an Emscripten capability, or an internal function exists or behaves as you think, say so and verify rather than asserting it confidently.
- **Summarize what changed and why.** End substantive work with a short summary: what changed, what was verified and how, any assumptions made, and anything noted-but-not-fixed under rule 3.
- **Ask before large or destructive moves.** Mass renames, file moves, deleting code that looks unused, or changing the build system are "ask first" actions even if they seem clearly correct.