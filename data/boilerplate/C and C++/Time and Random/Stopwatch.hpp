---
order: 10
description: A Stopwatch class measuring elapsed seconds or milliseconds with std::chrono::steady_clock
includes: <chrono>
---
// Starts timing when it's made. steady_clock never jumps when the system clock is changed, so it's the
// clock for measuring durations.
//     Stopwatch timer;
//     doWork();
//     std::cout << timer.milliseconds() << " ms\n";
class Stopwatch
{
    using Clock = std::chrono::steady_clock;

public:
    void restart() { start_ = Clock::now(); }

    double seconds() const { return std::chrono::duration<double>(Clock::now() - start_).count(); }

    long long milliseconds() const
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start_).count();
    }

private:
    Clock::time_point start_ = Clock::now();
};
