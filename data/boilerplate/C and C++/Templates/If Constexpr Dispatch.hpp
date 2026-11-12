---
label: Type Traits and if constexpr
order: 70
description: One template that picks code by type at compile time with if constexpr, type traits and requires
includes: <string> <type_traits>
---
// Only the branch that matches T is compiled for it, so each branch may use operations the other types
// lack (value.size(), std::to_string...).
template <typename T>
std::string describe(const T& value)
{
    if constexpr (std::is_same_v<T, bool>)
    {
        return value ? "true" : "false";
    }
    else if constexpr (std::is_arithmetic_v<T>)
    {
        return std::to_string(value);
    }
    else if constexpr (std::is_convertible_v<const T&, std::string>)
    {
        return "\"" + std::string(value) + "\"";
    }
    else if constexpr (requires { value.size(); })
    {
        return "a container of " + std::to_string(value.size()) + " elements";
    }
    else
    {
        // sizeof(T) == 0 is a false that depends on T, so this fires only if this branch is used.
        static_assert(sizeof(T) == 0, "describe() doesn't know this type");
    }
}
