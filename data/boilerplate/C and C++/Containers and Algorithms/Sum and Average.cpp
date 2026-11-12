---
label: Sum, Average and Transform-Reduce
order: 80
description: Adds numbers with std::accumulate, averages them, and totals a member with std::transform_reduce
includes: <functional> <numeric>
---
// The starting value sets the type of the sum: 0.0 adds doubles, while 0 would add ints and truncate.
const double sum = std::accumulate(values.begin(), values.end(), 0.0);
const double average = values.empty() ? 0.0 : sum / static_cast<double>(values.size());

// transform_reduce turns each element into a number and adds those up: here the cost of a cart.
const double cartTotal = std::transform_reduce(items.begin(), items.end(), 0.0, std::plus<>(),
                                               [](const Item& item) { return item.price * item.quantity; });
