---
label: String Formatting (std::format)
order: 70
description: std::format examples: order, width, alignment, precision, sign, hex/binary, fill (C++20)
includes: <format> <iostream> <string>
---
// {} takes the next argument; after a colon come fill, alignment, width, precision and type. A format
// string that doesn't match its arguments is a compile error.
const std::string label = std::format("Player {}: {} points", 1, 2500);
std::cout << label << '\n';
std::cout << std::format("{:.2f} | {:8.3f} | {:08.3f}\n", 3.14159, 2.5, 2.5);  // decimals, width, zeros
std::cout << std::format("[{:<8}] [{:>8}] [{:^8}]\n", "left", "right", "mid"); // alignment
std::cout << std::format("{:+} {:e}\n", 42, 1234.5);                           // sign, exponent
std::cout << std::format("{:x} {:#x} {:#b} {:o}\n", 255, 255, 5, 8);           // hex, 0x.., 0b.., octal
std::cout << std::format("{:*^20}\n", " title ");                              // fill character
std::cout << std::format("{1} before {0}\n", "second", "first");               // by position
