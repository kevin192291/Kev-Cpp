---
order: 30
description: Shuffles a container with std::ranges::shuffle and picks distinct random elements with sample
includes: <algorithm> <iterator> <random> <vector>
---
// Make the engine once and reuse it (see Random Numbers); a fixed seed such as engine{42} gives the same
// order on every run, which helps when testing.
std::mt19937 engine{std::random_device{}()};
std::ranges::shuffle(values, engine);

// Three different elements, in the order they have in values:
std::vector<int> picked;
std::ranges::sample(values, std::back_inserter(picked), 3, engine);
