---
label: std::format Support
order: 62
description: A std::formatter specialization so std::format("{}", value) prints your type (C++20)
ask: Type = Point
includes: <format>
---
// Lets std::format("{}", value) print a {{Type}}. Put it after {{Type}}, outside any namespace (it
// specializes std::formatter), and format the members it has.
template <>
struct std::formatter<{{Type}}>
{
    constexpr auto parse(std::format_parse_context& context) { return context.begin(); }

    auto format(const {{Type}}& value, std::format_context& context) const
    {
        return std::format_to(context.out(), "({}, {})", value.x, value.y);
    }
};
