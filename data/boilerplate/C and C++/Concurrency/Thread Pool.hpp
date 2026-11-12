---
order: 70
description: A fixed set of worker threads running queued tasks; submit() returns a std::future for the result
includes: <algorithm> <condition_variable> <functional> <future> <memory> <mutex> <queue> <thread> <type_traits> <vector>
---
//     ThreadPool pool;
//     std::future<int> answer = pool.submit([] { return 6 * 7; });
//     int value = answer.get(); // waits; rethrows if the task threw
// The destructor runs every task still queued, then joins the workers.
class ThreadPool
{
public:
    explicit ThreadPool(unsigned threadCount = std::max(1u, std::thread::hardware_concurrency()))
    {
        for (unsigned i = 0; i < threadCount; ++i)
        {
            workers_.emplace_back([this] { work(); });
        }
    }

    ~ThreadPool()
    {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
        }
        wake_.notify_all();
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    template <typename F>
    std::future<std::invoke_result_t<F>> submit(F task)
    {
        using Result = std::invoke_result_t<F>;
        // std::function needs a copyable function, and a packaged_task can only move: share it.
        auto packaged = std::make_shared<std::packaged_task<Result()>>(std::move(task));
        std::future<Result> result = packaged->get_future();
        {
            std::lock_guard lock(mutex_);
            tasks_.emplace([packaged] { (*packaged)(); });
        }
        wake_.notify_one();
        return result;
    }

private:
    void work()
    {
        while (true)
        {
            std::function<void()> task;
            {
                std::unique_lock lock(mutex_);
                wake_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });
                if (tasks_.empty())
                {
                    return; // stopping, and nothing left to do
                }
                task = std::move(tasks_.front());
                tasks_.pop();
            }
            task();
        }
    }

    std::mutex mutex_;
    std::condition_variable wake_;
    std::queue<std::function<void()>> tasks_;
    bool stopping_ = false;
    std::vector<std::jthread> workers_; // last: destroyed (joined) first, while the members above still exist
};
