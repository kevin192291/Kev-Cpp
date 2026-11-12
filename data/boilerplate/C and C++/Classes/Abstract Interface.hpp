---
order: 30
description: An interface of pure virtual functions with a virtual destructor, used through std::unique_ptr
ask: Interface = Shape
---
// An interface: implement it with Derived Class, use it through std::unique_ptr<{{Interface}}> or {{Interface}}&.
// The destructor is virtual so deleting through the interface destroys the whole derived object.
class {{Interface}}
{
public:
    virtual ~{{Interface}}() = default;

    virtual double area() const = 0;
    virtual void draw() const = 0;
    {{cursor}}

protected:
    // Copying through the interface would slice off the derived part; derived classes may still copy.
    {{Interface}}() = default;
    {{Interface}}(const {{Interface}}&) = default;
    {{Interface}}& operator=(const {{Interface}}&) = default;
};
