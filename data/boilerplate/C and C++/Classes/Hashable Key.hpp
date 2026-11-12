---
label: Hashable Key for unordered_map
order: 63
description: A struct with == and a std::hash specialization, usable as an unordered_map or unordered_set key
ask: Name = GridPos
includes: <cstddef> <functional>
---
// Use it as std::unordered_map<{{Name}}, Value> or std::unordered_set<{{Name}}>: a key needs == and a std::hash.
struct {{Name}}
{
    int x = 0;
    int y = 0;

    bool operator==(const {{Name}}&) const = default;
};

template <>
struct std::hash<{{Name}}>
{
    std::size_t operator()(const {{Name}}& key) const noexcept
    {
        std::size_t seed = std::hash<int>{}(key.x);
        // Mixes in each further member (boost::hash_combine's formula); repeat this line per member.
        seed ^= std::hash<int>{}(key.y) + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
        return seed;
    }
};
