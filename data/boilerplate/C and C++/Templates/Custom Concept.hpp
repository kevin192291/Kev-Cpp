---
order: 40
description: Defines a concept from a requires expression and uses it to constrain a template (C++20)
ask: Concept = Drawable
includes: <concepts> <string> <vector>
---
// A type is {{Concept}} when these expressions compile; static_assert({{Concept}}<MyType>) checks a type.
template <typename T>
concept {{Concept}} = requires(const T& item) {
    item.draw();                                         // has a draw() that works on a const object
    { item.name() } -> std::convertible_to<std::string>; // has a name() that gives something string-like
};

template <{{Concept}} T>
void drawAll(const std::vector<T>& items)
{
    for (const T& item : items)
    {
        item.draw();
    }
}
