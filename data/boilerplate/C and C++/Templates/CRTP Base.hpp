---
order: 60
description: A base class template that calls its derived class's functions without virtual calls (CRTP)
ask: Base = Entity
ask: Derived = Player
---
// The curiously recurring template pattern: {{Derived}} passes itself to {{Base}}, so the base can call
// {{Derived}}'s functions directly, with no virtual functions and no vtable.
template <typename Derived>
class {{Base}}
{
public:
    void update(double dt)
    {
        // Shared work goes here, around the derived class's part.
        self().onUpdate(dt);
    }

private:
    Derived& self() { return static_cast<Derived&>(*this); }

    // Private, with Derived a friend: `class B : public {{Base}}<A>` (a copy-paste slip) won't compile.
    {{Base}}() = default;
    friend Derived;
};

class {{Derived}} : public {{Base}}<{{Derived}}>
{
public:
    void onUpdate(double dt) { x_ += speed_ * dt; }
    double x() const { return x_; }

private:
    double x_ = 0.0;
    double speed_ = 1.0;
};
