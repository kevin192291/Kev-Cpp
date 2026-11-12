---
order: 61
description: An operator<< so std::cout << value prints your type
ask: Type = Point
includes: <ostream>
---
// Lets std::cout << value print a {{Type}}. Put it right after {{Type}}, in the same namespace, and
// print the members it has.
inline std::ostream& operator<<(std::ostream& out, const {{Type}}& value)
{
    return out << "{{Type}}(" << value.x << ", " << value.y << ')';
}
