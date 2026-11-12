---
label: Assertions (assert and static_assert)
order: 50
description: static_assert checked while compiling, assert checked at run time in Debug builds
includes: <cassert> <type_traits>
---
// static_assert stops the build when the condition is false. assert stops the program, but only in
// Debug builds: Release builds define NDEBUG, which removes it, so never put needed work inside one.
// For checks that must stay in Release builds, throw instead (see Check with Source Location).
static_assert(sizeof(int) >= 4, "this code needs ints of at least 32 bits");
static_assert(std::is_trivially_copyable_v<double>);
assert(!values.empty() && "values must not be empty here");
