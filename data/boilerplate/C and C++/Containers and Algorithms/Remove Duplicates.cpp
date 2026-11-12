---
order: 50
description: Sorts a vector and removes repeated values; notes how to keep the original order instead
includes: <algorithm>
---
// std::ranges::unique only removes repeats that are next to each other, hence the sort first.
std::ranges::sort(values);
const auto repeats = std::ranges::unique(values);
values.erase(repeats.begin(), repeats.end());

// To keep the first of each value in the original order instead (needs <unordered_set>):
//     std::unordered_set<int> seen;
//     std::erase_if(values, [&seen](int value) { return !seen.insert(value).second; });
