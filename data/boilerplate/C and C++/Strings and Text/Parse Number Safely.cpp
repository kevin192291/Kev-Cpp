---
label: Parse Number Safely (from_chars)
order: 60
description: parseNumber<int>("42") with std::from_chars: no exceptions, no locale, rejects partial numbers
includes: <charconv> <optional> <string_view>
---
// parseNumber<int>("42") gives 42; "42abc", "", " 42", "+42" and values that don't fit in T give
// std::nullopt. Works for every integer type and for float and double. Unlike std::stoi, it never
// throws and doesn't depend on the locale.
template <typename T>
std::optional<T> parseNumber(std::string_view text)
{
    T value{};
    const char* const last = text.data() + text.size();
    const auto [end, error] = std::from_chars(text.data(), last, value);
    if (error != std::errc() || end != last)
    {
        return std::nullopt;
    }
    return value;
}
