---
label: std::async and Futures
order: 60
description: Runs tasks on other threads with std::async and collects their results (and exceptions) with get()
includes: <future> <vector>
---
// std::launch::async starts each task on a new thread. get() waits for its result, and rethrows
// the exception if the task threw one. A future from std::async also waits in its destructor.
std::vector<std::future<long long>> parts;
for (int part = 0; part < 4; ++part)
{
    parts.push_back(std::async(std::launch::async, [part] {
        long long sum = 0;
        for (int i = part * 1000; i < (part + 1) * 1000; ++i)
        {
            sum += i;
        }
        return sum;
    }));
}

long long total = 0;
for (std::future<long long>& part : parts)
{
    total += part.get();
}
