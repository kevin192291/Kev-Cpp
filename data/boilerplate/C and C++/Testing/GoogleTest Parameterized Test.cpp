---
order: 20
description: A GoogleTest test run once for each row of values (TEST_P with INSTANTIATE_TEST_SUITE_P)
ask: Suite = Calculator
includes: <gtest/gtest.h> <tuple>
---
// The TEST_P body runs once per row given to INSTANTIATE_TEST_SUITE_P; GetParam() is the current row.
class {{Suite}}Cases : public ::testing::TestWithParam<std::tuple<int, int, int>>
{};

TEST_P({{Suite}}Cases, AddsCorrectly)
{
    const auto [a, b, expected] = GetParam();
    EXPECT_EQ(a + b, expected);
}

INSTANTIATE_TEST_SUITE_P(Rows, {{Suite}}Cases,
                         ::testing::Values(std::make_tuple(1, 2, 3), std::make_tuple(0, 0, 0),
                                           std::make_tuple(-1, 1, 0)));
