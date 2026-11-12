---
order: 70
description: Wraps the selected code in a timing loop and prints the average time per run
includes: <chrono> <iostream>
---
// Times the code in the loop over many runs. Measure a Release (-O2) build, and use what the code
// computes afterwards, or the optimiser may remove it.
{
    const int runs = 1000;
    const auto start = std::chrono::steady_clock::now();
    for (int run = 0; run < runs; ++run)
    {
        {{selection}}{{cursor}}
    }
    const std::chrono::duration<double, std::micro> elapsed = std::chrono::steady_clock::now() - start;
    std::cout << "average " << elapsed.count() / runs << " us per run (" << runs << " runs)\n";
}
