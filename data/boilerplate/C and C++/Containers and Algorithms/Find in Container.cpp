---
order: 40
description: Finds the first element that passes a test and gets its index; notes find, any_of and count_if
includes: <algorithm> <iostream> <iterator>
---
// The result is items.end() when nothing matches. For "member equals value" there's
// std::ranges::find(items, value, &Item::member); any_of and count_if answer "is there one?" and "how many?".
const auto it = std::ranges::find_if(items, [](const Item& item) { return item.name == "apple"; });
if (it != items.end())
{
    std::cout << "Found at index " << std::distance(items.begin(), it) << '\n';
    {{cursor}}
}
