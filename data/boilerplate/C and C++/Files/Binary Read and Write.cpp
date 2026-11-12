---
order: 80
description: Reads and writes raw bytes, and single trivially copyable values, in binary files
includes: <cstdint> <filesystem> <fstream> <istream> <ostream> <stdexcept> <type_traits> <vector>
---
// Values are written exactly as they sit in memory, so the file only reads back correctly on machines
// with the same byte order and type sizes; write each field in a fixed order for a portable format.
void writeBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
{
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    out.close();
    if (!out)
    {
        throw std::runtime_error("cannot write " + path.string());
    }
}

std::vector<std::uint8_t> readBytes(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        throw std::runtime_error("cannot open " + path.string());
    }
    std::vector<std::uint8_t> bytes(std::filesystem::file_size(path));
    in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!in)
    {
        throw std::runtime_error("cannot read " + path.string());
    }
    return bytes;
}

// One int, double or plain struct of those, at the stream's current position.
template <typename T>
void writeValue(std::ostream& out, const T& value)
{
    static_assert(std::is_trivially_copyable_v<T>, "only plain data can be written byte for byte");
    out.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

template <typename T>
T readValue(std::istream& in)
{
    static_assert(std::is_trivially_copyable_v<T>, "only plain data can be read byte for byte");
    T value{};
    in.read(reinterpret_cast<char*>(&value), sizeof(T));
    return value;
}
