---
label: Operator Overloads (2D Vector)
order: 60
description: A 2D vector with +, -, * and / operators, ==, length, normalized, dot and << printing
ask: Name = Vec2
includes: <cmath> <ostream>
---
// The binary operators are hidden friends built on the compound ones, so v + w, v * 2.0f and 2.0f * v
// all work and stay consistent.
struct {{Name}}
{
    float x = 0.0f;
    float y = 0.0f;

    {{Name}}& operator+=({{Name}} other)
    {
        x += other.x;
        y += other.y;
        return *this;
    }

    {{Name}}& operator-=({{Name}} other)
    {
        x -= other.x;
        y -= other.y;
        return *this;
    }

    {{Name}}& operator*=(float scale)
    {
        x *= scale;
        y *= scale;
        return *this;
    }

    {{Name}}& operator/=(float scale)
    {
        x /= scale;
        y /= scale;
        return *this;
    }

    {{Name}} operator-() const { return {-x, -y}; }
    bool operator==(const {{Name}}&) const = default;

    float length() const { return std::hypot(x, y); }

    // The same direction with length 1 (a zero vector stays zero).
    {{Name}} normalized() const
    {
        const float len = length();
        return len > 0.0f ? {{Name}}{x / len, y / len} : {{Name}}{};
    }

    friend {{Name}} operator+({{Name}} a, {{Name}} b) { return a += b; }
    friend {{Name}} operator-({{Name}} a, {{Name}} b) { return a -= b; }
    friend {{Name}} operator*({{Name}} v, float scale) { return v *= scale; }
    friend {{Name}} operator*(float scale, {{Name}} v) { return v *= scale; }
    friend {{Name}} operator/({{Name}} v, float scale) { return v /= scale; }
    friend float dot({{Name}} a, {{Name}} b) { return a.x * b.x + a.y * b.y; }

    friend std::ostream& operator<<(std::ostream& out, {{Name}} v) { return out << '(' << v.x << ", " << v.y << ')'; }
};
