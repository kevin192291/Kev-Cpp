---
order: 30
description: join(parts, ", ") for any container of strings, string_views or C strings
includes: <string> <string_view>
---
// join(std::vector<std::string>{"a", "b", "c"}, ", ") gives "a, b, c"; works for any container of
// std::string, std::string_view or const char*.
template <typename Range>
std::string join(const Range& parts, std::string_view separator)
{
    std::string result;
    bool first = true;
    for (const auto& part : parts)
    {
        if (!first)
        {
            result += separator;
        }
        result += part;
        first = false;
    }
    return result;
}
