---
order: 20
description: trim(), trimStart() and trimEnd() returning string_views, so nothing is copied
includes: <string_view>
---
// These return views into the original text: keep that string alive while you use the result, and
// write std::string(trim(text)) when you need a copy.
constexpr std::string_view whitespace = " \t\n\r\f\v";

std::string_view trimStart(std::string_view text)
{
    const std::size_t first = text.find_first_not_of(whitespace);
    return first == std::string_view::npos ? std::string_view() : text.substr(first);
}

std::string_view trimEnd(std::string_view text)
{
    const std::size_t last = text.find_last_not_of(whitespace);
    return last == std::string_view::npos ? std::string_view() : text.substr(0, last + 1);
}

std::string_view trim(std::string_view text)
{
    return trimEnd(trimStart(text));
}
