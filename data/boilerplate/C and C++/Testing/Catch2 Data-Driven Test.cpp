---
order: 40
description: A Catch2 v3 test case run once per row of a GENERATE(table<...>) of inputs and expected results
ask: Suite = Calculator
includes: <catch2/catch_test_macros.hpp> <catch2/generators/catch_generators.hpp>
---
// GENERATE runs the test case once for each row; CAPTURE prints the row's input when a check fails.
TEST_CASE("{{Suite}} squares numbers", "[{{Suite:kebab}}]")
{
    const auto [input, expected] = GENERATE(table<int, int>({
        {1, 1},
        {2, 4},
        {-3, 9},
    }));
    CAPTURE(input);
    REQUIRE(input * input == expected);
}
