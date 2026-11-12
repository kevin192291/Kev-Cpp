---
order: 50
description: One thread produces jobs and several consume them through ConcurrentQueue (Thread-Safe Queue)
includes: <iostream> <optional> <syncstream> <thread> <vector>
---
// Needs ConcurrentQueue from Boilerplate → C/C++ → Concurrency → Thread-Safe Queue.
ConcurrentQueue<int> jobs;
{
    std::vector<std::jthread> consumers;
    for (int id = 0; id < 3; ++id)
    {
        consumers.emplace_back([&jobs, id] {
            while (const std::optional<int> job = jobs.pop())
            {
                // osyncstream writes each line in one piece, so lines from different threads don't mix.
                std::osyncstream(std::cout) << "consumer " << id << " got job " << *job << '\n';
                {{cursor}}
            }
        });
    }

    for (int job = 1; job <= 10; ++job)
    {
        jobs.push(job);
    }
    jobs.close(); // the consumers finish what's queued, then their loops end
} // the jthreads are joined here
