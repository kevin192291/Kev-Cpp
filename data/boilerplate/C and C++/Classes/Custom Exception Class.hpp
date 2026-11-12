---
order: 45
description: An exception type derived from std::runtime_error that carries extra data (a line number)
ask: ClassName = ParseError
includes: <stdexcept> <string>
---
// Deriving from std::runtime_error keeps what() working and lets catch (const std::exception&) catch it.
// Without extra data, the class body can be just: using std::runtime_error::runtime_error;
class {{ClassName}} : public std::runtime_error
{
public:
    {{ClassName}}(const std::string& message, int line)
        : std::runtime_error(message + " (line " + std::to_string(line) + ")"), line_(line)
    {}

    int line() const noexcept { return line_; }

private:
    int line_;
};
