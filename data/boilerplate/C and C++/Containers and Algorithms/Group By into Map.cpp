---
order: 70
description: Groups elements by a key into a std::map of vectors, then loops over the groups
includes: <iostream> <map> <string> <vector>
---
// Puts each item in the list for its category; the map keeps the groups sorted by key.
std::map<std::string, std::vector<Item>> byCategory;
for (const Item& item : items)
{
    byCategory[item.category].push_back(item);
}
for (const auto& [category, group] : byCategory)
{
    std::cout << category << ": " << group.size() << " items\n";
    {{cursor}}
}
