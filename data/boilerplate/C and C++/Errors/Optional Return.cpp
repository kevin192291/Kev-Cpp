---
label: Return std::optional
order: 20
description: A function that may have no result returns std::optional; callers test it before using the value
includes: <optional> <string> <vector>
---
// The return type says "there may be no answer", and the caller can't use the value without looking:
//     if (const auto index = findIndex(names, "Ada"))
//         std::cout << "Ada is at " << *index << '\n';
//     const std::size_t i = findIndex(names, "Ada").value_or(0);
std::optional<std::size_t> findIndex(const std::vector<std::string>& names, const std::string& wanted)
{
    for (std::size_t i = 0; i < names.size(); ++i)
    {
        if (names[i] == wanted)
        {
            return i;
        }
    }
    return std::nullopt;
}
