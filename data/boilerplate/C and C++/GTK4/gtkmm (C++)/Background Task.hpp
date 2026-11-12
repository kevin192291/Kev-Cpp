---
order: 43
section: behaviour
description: A BackgroundTask class that runs slow work on a thread and hands the result back to the GTK thread
file: BackgroundTask.hpp
whole-file: yes
packages: gtkmm-4.0
---
// Runs slow work (a download, a big file, a long calculation) on another thread so the window keeps
// responding, then calls back on the GTK main thread, the only one allowed to touch widgets.
// Keep it as a member of the window that uses it:
//   BackgroundTask<std::string> loader_;
//   loader_.start([path] { return Glib::file_get_contents(path); },                 // on the worker thread
//                 [this](std::string text) { text_.get_buffer()->set_text(text); },   // back on the GTK thread
//                 [this](const std::string& error) { status_.set_text(error); });    // if the work threw
#pragma once

#include <glibmm.h>

#include <exception>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>

template <typename Result>
class BackgroundTask
{
public:
    BackgroundTask() { dispatcher_.connect(sigc::mem_fun(*this, &BackgroundTask::finish)); }

    // Waits for a running task to end, so nothing calls back into a destroyed window.
    ~BackgroundTask()
    {
        if (worker_.joinable())
        {
            worker_.join();
        }
    }

    BackgroundTask(const BackgroundTask&) = delete;
    BackgroundTask& operator=(const BackgroundTask&) = delete;

    bool running() const { return running_; }

    // Returns false, and does nothing, while the previous task is still running.
    bool start(std::function<Result()> work, std::function<void(Result)> done,
               std::function<void(const std::string&)> failed = {})
    {
        if (running_)
        {
            return false;
        }
        running_ = true;
        done_ = std::move(done);
        failed_ = std::move(failed);
        worker_ = std::thread([this, work = std::move(work)] {
            std::optional<Result> result;
            std::string error;
            try
            {
                result = work();
            }
            catch (const std::exception& e)
            {
                error = e.what();
            }
            catch (...)
            {
                error = "Unknown error";
            }
            {
                std::lock_guard lock(mutex_);
                result_ = std::move(result);
                error_ = std::move(error);
            }
            dispatcher_.emit(); // wakes finish() on the GTK thread
        });
        return true;
    }

private:
    void finish()
    {
        worker_.join();
        std::optional<Result> result;
        std::string error;
        {
            std::lock_guard lock(mutex_);
            result = std::move(result_);
            error = std::move(error_);
        }
        running_ = false;

        if (result)
        {
            if (done_)
            {
                done_(std::move(*result));
            }
        }
        else if (failed_)
        {
            failed_(error);
        }
        else
        {
            g_warning("BackgroundTask: %s", error.c_str());
        }
    }

    Glib::Dispatcher dispatcher_;
    std::thread worker_;
    std::mutex mutex_;
    std::optional<Result> result_;
    std::string error_;
    std::function<void(Result)> done_;
    std::function<void(const std::string&)> failed_;
    bool running_ = false;
};
