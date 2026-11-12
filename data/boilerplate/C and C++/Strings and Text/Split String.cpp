---
order: 10
description: split(text, ',') keeping empty fields, and splitWords(text) on any run of whitespace
includes: <iterator> <sstream> <string> <string_view> <vector>
---
// split("a,b,,c", ',') gives {"a", "b", "", "c"}: every separator counts, so empty fields are kept.
std::vector<std::string> split(std::string_view text, char separator)
{
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (true)
    {
        const std::size_t end = text.find(separator, start);
        parts.emplace_back(text.substr(start, end - start));
        if (end == std::string_view::npos)
        {
            return parts;
        }
        start = end + 1;
    }
}

// splitWords("  one two\tthree\n") gives {"one", "two", "three"}.
std::vector<std::string> splitWords(const std::string& text)
{
    std::istringstream in(text);
    return {std::istream_iterator<std::string>(in), std::istream_iterator<std::string>()};
}
