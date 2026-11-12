---
order: 90
description: Filters, transforms and takes elements with lazy std::views pipelines (C++20 ranges)
includes: <iostream> <ranges>
---
// Views are lazy: nothing runs until the loop asks for the next value, and nothing is copied.
// To keep the results: std::ranges::copy(evenSquares, std::back_inserter(result));
auto evenSquares = values
    | std::views::filter([](int n) { return n % 2 == 0; })
    | std::views::transform([](int n) { return n * n; })
    | std::views::take(5);
for (const int n : evenSquares)
{
    std::cout << n << ' ';
}
