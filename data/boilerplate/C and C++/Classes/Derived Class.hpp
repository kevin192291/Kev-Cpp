---
order: 31
description: A class implementing an interface, each virtual function marked override
ask: ClassName = Circle
ask: Base = Shape
includes: <iostream> <numbers>
---
// override makes the compiler check that each function really overrides one in {{Base}}.
class {{ClassName}} : public {{Base}}
{
public:
    explicit {{ClassName}}(double radius) : radius_(radius) {}

    double area() const override { return std::numbers::pi * radius_ * radius_; }
    void draw() const override { std::cout << "{{ClassName}} with radius " << radius_ << '\n'; }

private:
    double radius_;
};
