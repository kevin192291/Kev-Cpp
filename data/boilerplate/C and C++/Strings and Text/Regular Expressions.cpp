---
order: 90
description: std::regex: test a whole string, find every match with its groups, and replace with $1 references
includes: <iostream> <regex> <string>
---
// Patterns use ECMAScript syntax; raw strings R"(...)" save doubling every backslash. Building a
// std::regex is slow, so make each one once (static const) rather than in a loop.
static const std::regex datePattern(R"((\d{4})-(\d{2})-(\d{2}))");

const std::string text = "Released 2024-03-15, patched 2024-04-02.";
std::cout << std::boolalpha << std::regex_match("2024-03-15", datePattern) << '\n'; // the whole string?

for (auto it = std::sregex_iterator(text.begin(), text.end(), datePattern); it != std::sregex_iterator(); ++it)
{
    const std::smatch& match = *it;
    std::cout << "year " << match[1] << ", month " << match[2] << ", day " << match[3] << '\n';
}

std::cout << std::regex_replace(text, datePattern, "$3/$2/$1") << '\n'; // 15/03/2024 ...
