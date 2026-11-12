---
label: Enum Class with to_string and from_string
order: 40
description: An enum class with toString() and a from-string parser that returns std::optional
ask: Enum = Color
includes: <array> <optional> <string_view>
---
enum class {{Enum}}
{
    Red,
    Green,
    Blue,
};

// Every value, for loops and for parsing; add new values here too.
inline constexpr std::array all{{Enum}}Values = {
    {{Enum}}::Red,
    {{Enum}}::Green,
    {{Enum}}::Blue,
};

constexpr std::string_view toString({{Enum}} value)
{
    switch (value)
    {
    case {{Enum}}::Red:
        return "Red";
    case {{Enum}}::Green:
        return "Green";
    case {{Enum}}::Blue:
        return "Blue";
    }
    return "?"; // only reached for a value cast from an int that isn't in the enum
}

// {{Enum:camel}}FromString("Green") gives {{Enum}}::Green; unknown text gives std::nullopt.
constexpr std::optional<{{Enum}}> {{Enum:camel}}FromString(std::string_view text)
{
    for (const {{Enum}} value : all{{Enum}}Values)
    {
        if (toString(value) == text)
        {
            return value;
        }
    }
    return std::nullopt;
}
