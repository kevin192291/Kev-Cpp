---
order: 10
description: A GoogleTest file with plain tests and a fixture; the gtest_main package supplies main()
whole-file: yes
file: {{Suite:snake}}_test.cpp
ask: Suite = Calculator
packages: gtest_main
---
// Tests for {{Suite}} (GoogleTest; Ubuntu/Debian: apt install libgtest-dev). Build them as their own
// program: gtest_main supplies main(), which runs every TEST and reports the failures.
#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <vector>

TEST({{Suite}}Test, AddsNumbers)
{
    EXPECT_EQ(2 + 2, 4);
    {{cursor}}
}

TEST({{Suite}}Test, ComparesTextAndThrows)
{
    EXPECT_EQ(std::string("abc").size(), 3u);
    EXPECT_NEAR(0.1 + 0.2, 0.3, 1e-9); // floating point: compare within a tolerance
    EXPECT_THROW(std::vector<int>().at(0), std::out_of_range);
}

// A fixture: SetUp() runs before each TEST_F of it, so every test starts from the same state.
class {{Suite}}Fixture : public ::testing::Test
{
protected:
    void SetUp() override { values = {1, 2, 3}; }

    std::vector<int> values;
};

TEST_F({{Suite}}Fixture, StartsWithThreeValues)
{
    ASSERT_EQ(values.size(), 3u); // ASSERT_ ends this test when it fails; EXPECT_ carries on
    EXPECT_EQ(values.front(), 1);
}

TEST_F({{Suite}}Fixture, ClearEmptiesIt)
{
    values.clear();
    EXPECT_TRUE(values.empty());
}
