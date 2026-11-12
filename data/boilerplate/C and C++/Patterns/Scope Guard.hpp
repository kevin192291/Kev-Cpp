---
label: Scope Guard (RAII Cleanup)
order: 50
description: Runs a cleanup function when the scope ends, by return, break or exception, unless dismissed
includes: <utility>
---
//     std::FILE* file = std::fopen(name, "r");
//     ScopeGuard closeFile([&] { std::fclose(file); });
// Call dismiss() when the cleanup should no longer happen, e.g. a rollback once the commit succeeded.
template <typename F>
class ScopeGuard
{
public:
    explicit ScopeGuard(F onExit) : onExit_(std::move(onExit)) {}

    ~ScopeGuard()
    {
        if (active_)
        {
            onExit_();
        }
    }

    ScopeGuard(const ScopeGuard&) = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;

    void dismiss() noexcept { active_ = false; }

private:
    F onExit_;
    bool active_ = true;
};
