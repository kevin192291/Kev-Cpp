---
order: 120
description: A fixed-capacity circular buffer that overwrites its oldest element when full
ask: ClassName = RingBuffer
includes: <array> <cstddef> <optional> <utility>
---
// Keeps the last Capacity values (recent samples, log lines, input history) without allocating;
// pushing into a full buffer overwrites the oldest. Index 0 is the oldest element.
template <typename T, std::size_t Capacity>
class {{ClassName}}
{
    static_assert(Capacity > 0);

public:
    void push(T value)
    {
        items_[(start_ + size_) % Capacity] = std::move(value);
        if (size_ < Capacity)
        {
            ++size_;
        }
        else
        {
            start_ = (start_ + 1) % Capacity; // full: the oldest was just overwritten
        }
    }

    // Takes out the oldest element, or std::nullopt when empty.
    std::optional<T> pop()
    {
        if (size_ == 0)
        {
            return std::nullopt;
        }
        T value = std::move(items_[start_]);
        start_ = (start_ + 1) % Capacity;
        --size_;
        return value;
    }

    const T& operator[](std::size_t i) const { return items_[(start_ + i) % Capacity]; }

    std::size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    bool full() const { return size_ == Capacity; }

private:
    std::array<T, Capacity> items_{};
    std::size_t start_ = 0;
    std::size_t size_ = 0;
};
