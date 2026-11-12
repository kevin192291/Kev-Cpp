---
order: 20
description: readLines(path) returns a file's lines in a vector, without line endings (\n or \r\n)
includes: <filesystem> <fstream> <stdexcept> <string> <utility> <vector>
---
std::vector<std::string> readLines(const std::filesystem::path& path)
{
    std::ifstream in(path);
    if (!in)
    {
        throw std::runtime_error("cannot open " + path.string());
    }
    std::vector<std::string> lines;
    for (std::string line; std::getline(in, line);)
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back(); // a file saved on Windows ends its lines with \r\n
        }
        lines.push_back(std::move(line));
    }
    return lines;
}
