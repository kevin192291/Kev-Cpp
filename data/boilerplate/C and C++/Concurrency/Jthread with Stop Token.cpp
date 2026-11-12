---
label: std::jthread with stop_token
order: 20
description: A background loop in a std::jthread that ends when asked to stop, and joins itself (C++20)
includes: <chrono> <stop_token> <thread>
---
// A std::jthread asks its function to stop and joins it when it's destroyed, so it can't be left running
// by accident. Check the token often enough that stopping doesn't keep the caller waiting long.
std::jthread worker([](std::stop_token stop) {
    while (!stop.stop_requested())
    {
        {{cursor}}
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
});
// ... later, or let the destructor do both:
worker.request_stop();
worker.join();
