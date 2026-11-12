---
label: Observer (Signal and Slots)
order: 10
description: A small signal class: connect callbacks, emit to call them all, disconnect by id
includes: <cstddef> <functional> <map> <utility>
---
//     Signal<int, int> scoreChanged;
//     const auto id = scoreChanged.connect([](int oldScore, int newScore) { ... });
//     scoreChanged.emit(10, 20);
//     scoreChanged.disconnect(id);
template <typename... Args>
class Signal
{
public:
    using Slot = std::function<void(Args...)>;

    std::size_t connect(Slot slot)
    {
        slots_.emplace(nextId_, std::move(slot));
        return nextId_++;
    }

    void disconnect(std::size_t id) { slots_.erase(id); }

    // Calls the slots in the order they were connected. It works on a copy, so a slot may connect or
    // disconnect slots while it runs (changes take effect from the next emit).
    void emit(Args... args) const
    {
        const auto slots = slots_;
        for (const auto& entry : slots)
        {
            entry.second(args...);
        }
    }

private:
    std::map<std::size_t, Slot> slots_;
    std::size_t nextId_ = 0;
};
