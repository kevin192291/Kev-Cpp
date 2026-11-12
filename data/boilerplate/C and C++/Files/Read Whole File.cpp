---
order: 10
description: readFile(path) returns a file's whole contents as a std::string; throws if it can't be read
includes: <filesystem> <fstream> <sstream> <stdexcept> <string>
---
std::string readFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        throw std::runtime_error("cannot open " + path.string());
    }
    std::ostringstream contents;
    contents << in.rdbuf();
    return contents.str();
}
