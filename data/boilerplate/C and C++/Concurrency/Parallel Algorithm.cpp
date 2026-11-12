---
label: Parallel Algorithms (Execution Policies)
order: 90
description: std::sort, std::reduce and std::transform with execution policies; g++ runs them in parallel only with TBB
includes: <algorithm> <execution> <numeric>
---
// std::execution::par lets the library split the work over threads (par_unseq may also vectorise it).
// With g++ that only happens when Intel TBB is installed (libtbb-dev) and the program links it (add the
// tbb package); without TBB the same code compiles and runs on one thread.
std::sort(std::execution::par, values.begin(), values.end());
const double sum = std::reduce(std::execution::par, values.begin(), values.end(), 0.0);
std::transform(std::execution::par_unseq, values.begin(), values.end(), values.begin(),
               [](double value) { return value * 2.0; });
