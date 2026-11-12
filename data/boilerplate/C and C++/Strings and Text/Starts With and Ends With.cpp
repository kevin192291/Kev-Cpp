---
order: 50
description: C++20 starts_with/ends_with, removing a prefix, and contains() via find
includes: <string_view>
---
// std::string and std::string_view have starts_with() and ends_with() since C++20; contains() only
// arrives in C++23, so it's find() != npos until then.
bool isSourceFile(std::string_view fileName)
{
    return fileName.ends_with(".cpp") || fileName.ends_with(".hpp") || fileName.ends_with(".h");
}

// removePrefix("--name=value", "--") gives "name=value"; text without the prefix comes back unchanged.
std::string_view removePrefix(std::string_view text, std::string_view prefix)
{
    if (text.starts_with(prefix))
    {
        text.remove_prefix(prefix.size());
    }
    return text;
}

bool contains(std::string_view text, std::string_view part)
{
    return text.find(part) != std::string_view::npos;
}
