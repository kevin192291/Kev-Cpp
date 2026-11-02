#include "process/Capture.h"

#include <giomm.h>

namespace kevc {

void captureOutput(const std::vector<std::string>& argv, const std::filesystem::path& cwd,
                   std::function<void(std::string, int)> done, std::weak_ptr<int> alive) {
    Glib::RefPtr<Gio::Subprocess> process;
    try {
        auto launcher = Gio::SubprocessLauncher::create(Gio::Subprocess::Flags::STDOUT_PIPE | Gio::Subprocess::Flags::STDERR_SILENCE);
        launcher->set_cwd(cwd.string());
        process = launcher->spawn(argv);
    } catch (const Glib::Error&) {
        done({}, -1);
        return;
    }
    process->communicate_async(
        {},
        [process, done, alive](const Glib::RefPtr<Gio::AsyncResult>& result) {
            std::string out;
            try {
                auto [stdoutBytes, stderrBytes] = process->communicate_finish(result);
                if (stdoutBytes) {
                    gsize size = 0;
                    const auto* data = static_cast<const char*>(stdoutBytes->get_data(size));
                    if (data) out.assign(data, size);
                }
            } catch (const Glib::Error&) {
            }
            if (alive.expired()) return;
            done(std::move(out), process->get_if_exited() ? process->get_exit_status() : -1);
        });
}

void captureOutput(const std::vector<std::string>& argv, const std::filesystem::path& cwd, const std::string& input,
                   std::function<void(std::string, std::string, int)> done, std::weak_ptr<int> alive) {
    Glib::RefPtr<Gio::Subprocess> process;
    try {
        auto launcher = Gio::SubprocessLauncher::create(Gio::Subprocess::Flags::STDIN_PIPE | Gio::Subprocess::Flags::STDOUT_PIPE |
                                                        Gio::Subprocess::Flags::STDERR_PIPE);
        launcher->set_cwd(cwd.string());
        process = launcher->spawn(argv);
    } catch (const Glib::Error& e) {
        done({}, e.what(), -1);
        return;
    }
    auto bytes = [](const Glib::RefPtr<Glib::Bytes>& b) {
        std::string out;
        gsize size = 0;
        if (b)
            if (const auto* data = static_cast<const char*>(b->get_data(size))) out.assign(data, size);
        return out;
    };
    process->communicate_async(
        Glib::Bytes::create(input.data(), input.size()),
        [process, done, alive, bytes](const Glib::RefPtr<Gio::AsyncResult>& result) {
            std::string out, err;
            try {
                auto [stdoutBytes, stderrBytes] = process->communicate_finish(result);
                out = bytes(stdoutBytes);
                err = bytes(stderrBytes);
            } catch (const Glib::Error& e) {
                err = e.what();
            }
            if (alive.expired()) return;
            done(std::move(out), std::move(err), process->get_if_exited() ? process->get_exit_status() : -1);
        });
}

} // namespace kevc
