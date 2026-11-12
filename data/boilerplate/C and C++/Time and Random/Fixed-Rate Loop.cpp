---
order: 50
description: Runs a loop body at a steady rate (every 100 ms) with sleep_until, without drifting
includes: <chrono> <thread>
---
// Each pass sleeps until a deadline that moves on by exactly one period, so the time the work takes
// doesn't add up as drift (as sleeping 100 ms after the work would).
const auto period = std::chrono::milliseconds(100);
auto deadline = std::chrono::steady_clock::now();
for (int tick = 0; tick < 50; ++tick)
{
    {{cursor}}
    deadline += period;
    std::this_thread::sleep_until(deadline);
}
