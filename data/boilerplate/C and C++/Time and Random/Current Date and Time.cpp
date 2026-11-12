---
order: 60
description: The local date and time formatted with std::format and C++20 time zones, plus UTC in ISO 8601
includes: <chrono> <format> <iostream>
---
// Uses the C++20 time zone database (g++ 13 or newer). The % fields are strftime's: %Y year, %m month,
// %d day, %H:%M:%S time, %A weekday, %B month name, %Z zone.
const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
const std::chrono::zoned_time local{std::chrono::current_zone(), now};
std::cout << std::format("{:%Y-%m-%d %H:%M:%S %Z}", local) << '\n'; // 2026-03-15 14:30:00 CET
std::cout << std::format("{:%A %d %B %Y}", local) << '\n';          // Sunday 15 March 2026
std::cout << std::format("{:%FT%TZ}", now) << '\n';                 // UTC: 2026-03-15T13:30:00Z

// The date as numbers:
const std::chrono::year_month_day today{std::chrono::floor<std::chrono::days>(local.get_local_time())};
const int year = static_cast<int>(today.year());
const unsigned month = static_cast<unsigned>(today.month());
const unsigned day = static_cast<unsigned>(today.day());
