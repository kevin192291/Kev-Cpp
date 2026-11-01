#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <thread>

namespace kevc {

// One piece of work at a time on a thread of its own (listing or searching a project's files),
// handing results back to the main loop. start() cancels the job before it and waits for it, and
// so does the destructor: a job never outlives its owner, and nothing a cancelled job posted runs.
class BackgroundJob {
public:
    // Runs `fn` on the main loop, unless the job is cancelled before then. Called from the job.
    using Post = std::function<void(std::function<void()> fn)>;
    using Work = std::function<void(const std::atomic<bool>& cancelled, const Post& post)>;

    BackgroundJob() = default;
    BackgroundJob(const BackgroundJob&) = delete;
    BackgroundJob& operator=(const BackgroundJob&) = delete;
    ~BackgroundJob() { cancel(); }

    void start(Work work);
    // Asks the job to stop and waits for its thread (work checks `cancelled` often, so it's quick).
    void cancel();

private:
    std::thread thread_;
    std::shared_ptr<std::atomic<bool>> cancelled_;
};

} // namespace kevc
