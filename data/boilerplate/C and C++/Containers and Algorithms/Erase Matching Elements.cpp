---
label: Erase Matching Elements (erase_if)
order: 20
description: Removes every element that passes a test with std::erase_if (C++20; replaces erase-remove)
includes: <vector>
---
// Removes every element the test accepts and returns how many went (C++20). Works on vector, deque, list,
// string, map, set and the unordered containers; for a map the test gets the key-value pair.
// Before C++20: values.erase(std::remove_if(values.begin(), values.end(), test), values.end());
std::erase_if(values, [](int value) { return value < 0; });
