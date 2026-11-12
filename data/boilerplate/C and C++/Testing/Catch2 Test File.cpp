---
order: 30
description: A Catch2 v3 test file with test cases, sections and a floating-point matcher; catch2-with-main supplies main()
whole-file: yes
file: {{Suite:snake}}_test.cpp
ask: Suite = Calculator
packages: catch2-with-main
---
// Tests for {{Suite}} (Catch2 v3; Ubuntu/Debian: apt install catch2). Build them as their own program:
// catch2-with-main supplies main(), which runs every TEST_CASE (or those with a tag: ./tests "[tag]").
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <stdexcept>
#include <vector>

TEST_CASE("{{Suite}} adds numbers", "[{{Suite:kebab}}]")
{
    REQUIRE(2 + 2 == 4);
    {{cursor}}
}

TEST_CASE("{{Suite}} keeps a list of values", "[{{Suite:kebab}}]")
{
    std::vector<int> values{1, 2, 3};

    // Each SECTION runs on its own, starting again from the top of the TEST_CASE.
    SECTION("push_back adds a value")
    {
        values.push_back(4);
        REQUIRE(values.size() == 4);
    }
    SECTION("clear empties it")
    {
        values.clear();
        CHECK(values.empty()); // CHECK carries on after a failure; REQUIRE ends the test case
    }
}

TEST_CASE("{{Suite}} compares and throws", "[{{Suite:kebab}}]")
{
    using Catch::Matchers::WithinRel;
    REQUIRE_THAT(0.1 + 0.2, WithinRel(0.3, 1e-9)); // floating point: compare within a tolerance
    REQUIRE_THROWS_AS(std::vector<int>().at(0), std::out_of_range);
}
