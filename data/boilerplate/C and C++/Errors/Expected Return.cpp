---
label: Return std::expected (C++23)
order: 30
description: Returns a value or an error message with std::expected; needs the C++23 standard (g++ 13 or newer)
includes: <charconv> <expected> <string> <string_view>
---
// Needs the project's C++ standard set to C++23 (Project Options).
// The result holds either the port or the reason there isn't one, so failure is part of the type:
//     const auto port = parsePort(text);
//     if (port)
//         connect(*port);
//     else
//         std::cerr << port.error() << '\n';
std::expected<int, std::string> parsePort(std::string_view text)
{
    int port = 0;
    const char* const last = text.data() + text.size();
    const auto [end, error] = std::from_chars(text.data(), last, port);
    if (error != std::errc() || end != last)
    {
        return std::unexpected("not a number: " + std::string(text));
    }
    if (port < 1 || port > 65535)
    {
        return std::unexpected("port out of range: " + std::to_string(port));
    }
    return port;
}
