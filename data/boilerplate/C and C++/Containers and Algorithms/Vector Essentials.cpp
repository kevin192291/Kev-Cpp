---
order: 10
description: The everyday std::vector operations: add, insert, erase, sort, size, front/back and indexed loops
includes: <algorithm> <iostream> <vector>
---
std::vector<int> values = {5, 3, 8, 1};
values.reserve(100);              // room for 100 without reallocating (optional)
values.push_back(42);             // add at the end
values.insert(values.begin(), 0); // add at the front: moves every element, so slow for big vectors
values.erase(values.begin() + 2); // remove by position
std::erase(values, 8);            // remove every 8 (C++20)
std::ranges::sort(values);

std::cout << values.size() << " values, first " << values.front() << ", last " << values.back() << '\n';
for (std::size_t i = 0; i < values.size(); ++i)
{
    std::cout << i << ": " << values.at(i) << '\n'; // at() checks the index, [] doesn't
}
{{cursor}}
