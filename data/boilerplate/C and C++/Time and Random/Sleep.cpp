---
order: 40
description: Pauses the current thread for a duration with std::this_thread::sleep_for
includes: <chrono> <thread>
---
// With `using namespace std::chrono_literals;` this can be written sleep_for(500ms).
std::this_thread::sleep_for(std::chrono::milliseconds(500));
