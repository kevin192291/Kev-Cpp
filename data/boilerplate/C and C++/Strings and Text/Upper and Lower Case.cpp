---
order: 40
description: toUpper, toLower and a case-insensitive comparison for ASCII text
includes: <algorithm> <cctype> <string> <string_view>
---
// ASCII only: std::toupper works one byte at a time, so letters like é or ß in UTF-8 text stay as they
// are. The unsigned char parameters matter: passing a negative char to std::toupper is undefined.
std::string toUpper(std::string text)
{
    std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return text;
}

std::string toLower(std::string text)
{
    std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool equalsIgnoreCase(std::string_view a, std::string_view b)
{
    const auto sameLetter = [](unsigned char x, unsigned char y) { return std::tolower(x) == std::tolower(y); };
    return std::ranges::equal(a, b, sameLetter);
}
