---
order: 30
description: writeFile(path, text) creates or replaces a file and throws if the text didn't get written
includes: <filesystem> <fstream> <stdexcept> <string_view>
---
// Replaces the file's contents, creating it if needed (not its folder: see create_directories).
// Errors such as a full disk only show once the data is flushed, hence the check after close().
void writeFile(const std::filesystem::path& path, std::string_view text)
{
    std::ofstream out(path, std::ios::binary);
    if (!out)
    {
        throw std::runtime_error("cannot create " + path.string());
    }
    out << text;
    out.close();
    if (!out)
    {
        throw std::runtime_error("cannot write " + path.string());
    }
}
