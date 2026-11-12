---
order: 40
description: A blocking queue for passing work between threads, with close() to tell consumers to finish
includes: <condition_variable> <mutex> <optional> <queue> <utility>
---
// Any number of threads may push and pop. pop() waits until there's an item; once close() is called and
// the queue is empty it returns std::nullopt, which tells consumers to stop.
template <typename T>
class ConcurrentQueue
{
public:
    void push(T item)
    {
        {
            std::lock_guard lock(mutex_);
            items_.push(std::move(item));
        }
        ready_.notify_one();
    }

    std::optional<T> pop()
    {
        std::unique_lock lock(mutex_);
        ready_.wait(lock, [this] { return !items_.empty() || closed_; });
        if (items_.empty())
        {
            return std::nullopt; // closed and drained
        }
        T item = std::move(items_.front());
        items_.pop();
        return item;
    }

    // Doesn't wait: std::nullopt when there's nothing to take right now.
    std::optional<T> tryPop()
    {
        std::lock_guard lock(mutex_);
        if (items_.empty())
        {
            return std::nullopt;
        }
        T item = std::move(items_.front());
        items_.pop();
        return item;
    }

    void close()
    {
        {
            std::lock_guard lock(mutex_);
            closed_ = true;
        }
        ready_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable ready_;
    std::queue<T> items_;
    bool closed_ = false;
};
