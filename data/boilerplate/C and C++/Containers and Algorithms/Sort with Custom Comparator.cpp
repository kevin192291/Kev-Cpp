---
order: 30
description: Sorts by several members with a lambda; by one member with a projection, ascending or descending
includes: <algorithm> <functional> <tuple>
---
// By category, then by name. For a single member, a projection is shorter:
//     std::ranges::sort(items, {}, &Item::price);                     // cheapest first
//     std::ranges::sort(items, std::ranges::greater{}, &Item::price); // dearest first
// std::ranges::stable_sort keeps elements that compare equal in the order they were.
std::ranges::sort(items, [](const Item& a, const Item& b) {
    return std::tie(a.category, a.name) < std::tie(b.category, b.name);
});
