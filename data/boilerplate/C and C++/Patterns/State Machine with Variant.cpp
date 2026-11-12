---
label: State Machine with std::variant
order: 31
description: Each state a struct with its own data, held in a std::variant and handled with std::visit
includes: <string> <variant>
---
// Each state is a struct that holds only the data that state needs. std::visit with an overload for
// every state won't compile if a state is left out.
template <typename... Fs>
struct Overloaded : Fs...
{
    using Fs::operator()...;
};

struct Idle
{};
struct Connecting
{
    int attempt = 1;
};
struct Connected
{
    std::string address;
};
struct Failed
{
    std::string reason;
};

using ConnectionState = std::variant<Idle, Connecting, Connected, Failed>;

std::string describe(const ConnectionState& state)
{
    const auto text = Overloaded{
        [](const Idle&) -> std::string { return "idle"; },
        [](const Connecting& s) -> std::string { return "connecting, attempt " + std::to_string(s.attempt); },
        [](const Connected& s) -> std::string { return "connected to " + s.address; },
        [](const Failed& s) -> std::string { return "failed: " + s.reason; },
    };
    return std::visit(text, state);
}

// The transition for one event; the generic lambda keeps every state the event doesn't apply to.
ConnectionState onAttemptFinished(const ConnectionState& state, bool succeeded, const std::string& address)
{
    const auto next = Overloaded{
        [&](const Connecting& s) -> ConnectionState {
            if (succeeded)
            {
                return Connected{address};
            }
            if (s.attempt < 3)
            {
                return Connecting{s.attempt + 1};
            }
            return Failed{"no answer after 3 attempts"};
        },
        [](const auto& other) -> ConnectionState { return other; },
    };
    return std::visit(next, state);
}
