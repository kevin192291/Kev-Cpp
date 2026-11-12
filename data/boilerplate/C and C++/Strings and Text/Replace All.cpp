---
order: 80
description: replaceAll(text, from, to) replacing every occurrence of a substring
includes: <string> <string_view>
---
// replaceAll("a-b-c", "-", " + ") gives "a + b + c". The search continues after each replacement, so a
// replacement that contains the searched text can't loop forever.
std::string replaceAll(std::string text, std::string_view from, std::string_view to)
{
    if (from.empty())
    {
        return text;
    }
    std::size_t position = 0;
    while ((position = text.find(from, position)) != std::string::npos)
    {
        text.replace(position, from.size(), to);
        position += to.size();
    }
    return text;
}
