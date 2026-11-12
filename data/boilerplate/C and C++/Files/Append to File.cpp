---
order: 40
description: appendLine(path, line) adds a line at the end of a file, creating it if missing (logs)
includes: <filesystem> <fstream> <stdexcept> <string_view>
---
void appendLine(const std::filesystem::path& path, std::string_view line)
{
    std::ofstream out(path, std::ios::app);
    if (!out)
    {
        throw std::runtime_error("cannot open " + path.string());
    }
    out << line << '\n';
    out.close();
    if (!out)
    {
        throw std::runtime_error("cannot write " + path.string());
    }
}
