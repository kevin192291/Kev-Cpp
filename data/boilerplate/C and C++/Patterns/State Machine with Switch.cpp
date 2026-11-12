---
label: State Machine (enum and switch)
order: 30
description: States and events as enum classes, with one function that says which state an event leads to
---
// Every state and event is named, and every transition is in one place: nextState(). Events that
// don't apply in a state leave it unchanged. The compiler warns (-Wall) if a state is missing.
enum class State
{
    Idle,
    Running,
    Paused,
    Finished,
};

enum class Event
{
    Start,
    Pause,
    Resume,
    Finish,
    Reset,
};

State nextState(State state, Event event)
{
    switch (state)
    {
    case State::Idle:
        if (event == Event::Start)
        {
            return State::Running;
        }
        break;
    case State::Running:
        if (event == Event::Pause)
        {
            return State::Paused;
        }
        if (event == Event::Finish)
        {
            return State::Finished;
        }
        break;
    case State::Paused:
        if (event == Event::Resume)
        {
            return State::Running;
        }
        if (event == Event::Reset)
        {
            return State::Idle;
        }
        break;
    case State::Finished:
        if (event == Event::Reset)
        {
            return State::Idle;
        }
        break;
    }
    return state;
}
