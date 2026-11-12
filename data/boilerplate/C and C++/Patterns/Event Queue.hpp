---
order: 90
description: Events as a std::variant, posted now and dispatched later (once per frame) to a visitor
includes: <deque> <utility> <variant>
---
// Code that notices something posts an event; the main loop dispatches them all at a moment of its
// choosing. The handler needs an operator() for every event type:
//     queue.dispatch([](const auto& event) {
//         if constexpr (std::is_same_v<std::decay_t<decltype(event)>, KeyPressed>) { ... }
//     });
struct KeyPressed
{
    int key = 0;
};
struct WindowResized
{
    int width = 0;
    int height = 0;
};
struct QuitRequested
{};

using Event = std::variant<KeyPressed, WindowResized, QuitRequested>;

class EventQueue
{
public:
    void post(Event event) { pending_.push_back(std::move(event)); }

    // Handles the events posted so far; events posted by the handler wait for the next dispatch().
    template <typename Handler>
    void dispatch(Handler&& handler)
    {
        std::deque<Event> batch;
        batch.swap(pending_);
        for (const Event& event : batch)
        {
            std::visit(handler, event);
        }
    }

    bool empty() const { return pending_.empty(); }

private:
    std::deque<Event> pending_;
};
