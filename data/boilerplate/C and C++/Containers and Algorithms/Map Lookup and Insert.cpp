---
order: 15
description: std::map insert, overwrite, insert-if-missing, lookup without inserting, erase and iteration
includes: <iostream> <map> <string>
---
// std::map keeps its keys sorted; std::unordered_map has the same functions and keeps no order.
std::map<std::string, int> ages;
ages["Ada"] = 36;            // inserts, or overwrites the value of an existing key
ages.try_emplace("Ada", 99); // inserts only when the key is missing, so Ada stays 36
ages.try_emplace("Alan", 41);

// Look up with find() (or at(), which throws): [] would insert a missing key.
if (const auto it = ages.find("Grace"); it != ages.end())
{
    std::cout << "Grace is " << it->second << '\n';
}
if (ages.contains("Alan")) // C++20
{
    ages.erase("Alan");
}

for (const auto& [name, age] : ages)
{
    std::cout << name << ": " << age << '\n';
}
