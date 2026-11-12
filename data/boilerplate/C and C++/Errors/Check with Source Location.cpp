---
order: 60
description: check(condition, message) that throws with the caller's file and line (C++20 source_location)
includes: <source_location> <stdexcept> <string> <string_view>
---
// Like assert, but it stays in Release builds and throws std::logic_error naming the file and line of
// the failed check:
//     check(index < items.size(), "index out of range");
void check(bool condition, std::string_view message, const std::source_location where = std::source_location::current())
{
    if (!condition)
    {
        throw std::logic_error(std::string(where.file_name()) + ":" + std::to_string(where.line()) + ": " +
                               std::string(message));
    }
}
