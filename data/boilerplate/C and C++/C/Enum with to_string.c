---
description: An enum and a function that names its values, for printing and logging
ask: Name = Color
order: 35
section: data
---
typedef enum {{Name:pascal}}
{
    {{Name:snake:upper}}_RED,
    {{Name:snake:upper}}_GREEN,
    {{Name:snake:upper}}_BLUE,
} {{Name:pascal}};

// No default: case, so -Wall warns here when a new value has no name yet.
const char* {{Name:snake}}_to_string({{Name:pascal}} value)
{
    switch (value)
    {
    case {{Name:snake:upper}}_RED:
        return "red";
    case {{Name:snake:upper}}_GREEN:
        return "green";
    case {{Name:snake:upper}}_BLUE:
        return "blue";
    }
    return "unknown";
}
