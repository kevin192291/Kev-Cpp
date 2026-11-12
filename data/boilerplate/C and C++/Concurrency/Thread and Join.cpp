---
order: 10
description: Starts several std::threads, each writing its own result, and waits for all of them
includes: <thread> <vector>
---
// Each thread writes only its own element of results, so they need no lock. Lambdas capture by value
// unless told otherwise; capture shared data by reference only while it outlives the threads.
const int threadCount = 4;
std::vector<long long> results(threadCount);
std::vector<std::thread> threads;
for (int t = 0; t < threadCount; ++t)
{
    threads.emplace_back([t, &results] {
        long long sum = 0;
        for (int i = t * 1000; i < (t + 1) * 1000; ++i)
        {
            sum += i;
        }
        results[t] = sum;
    });
}
for (std::thread& thread : threads)
{
    thread.join(); // a std::thread must be joined (or detached) before it's destroyed
}
