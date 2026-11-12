---
order: 80
description: A std::atomic counter incremented from several threads without a mutex
includes: <atomic> <thread> <vector>
---
// std::atomic makes ++ and fetch_add safe from many threads without a mutex. That suits counters and
// flags; when several values must change together, use a mutex.
std::atomic<long long> counter = 0;
{
    std::vector<std::jthread> threads;
    for (int t = 0; t < 4; ++t)
    {
        threads.emplace_back([&counter] {
            for (int i = 0; i < 100'000; ++i)
            {
                counter.fetch_add(1, std::memory_order_relaxed); // relaxed: only the final count matters
            }
        });
    }
} // the jthreads are joined here, so counter.load() is now 400000
