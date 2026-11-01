#include "process/BackgroundJob.h"

#include <glib.h>

namespace kevc {

namespace {

struct Posted {
    std::shared_ptr<std::atomic<bool>> cancelled;
    std::function<void()> fn;
};

gboolean runPosted(gpointer data) {
    auto* posted = static_cast<Posted*>(data);
    // Checked on the main loop, where cancel() runs too: once it has, the owner may be gone.
    if (!*posted->cancelled) posted->fn();
    return G_SOURCE_REMOVE;
}

void freePosted(gpointer data) {
    delete static_cast<Posted*>(data);
}

} // namespace

void BackgroundJob::start(Work work) {
    cancel();
    cancelled_ = std::make_shared<std::atomic<bool>>(false);
    thread_ = std::thread([flag = cancelled_, work = std::move(work)] {
        const Post post = [flag](std::function<void()> fn) {
            // g_idle_add_full is safe from any thread; below redraw priority, so typing stays smooth.
            g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, &runPosted, new Posted{flag, std::move(fn)}, &freePosted);
        };
        work(*flag, post);
    });
}

void BackgroundJob::cancel() {
    if (cancelled_) *cancelled_ = true;
    if (thread_.joinable()) thread_.join();
}

} // namespace kevc
