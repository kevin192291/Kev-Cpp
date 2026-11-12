---
order: 60
description: listFolder(path) returns a folder's files and subfolders sorted by name (std::filesystem)
includes: <algorithm> <filesystem> <vector>
---
// One level only (Recursive File Search goes into subfolders). Sorted, because the file system's own
// order is arbitrary. Throws std::filesystem::filesystem_error if the folder can't be read.
//     for (const auto& entry : listFolder("."))
//         std::cout << (entry.is_directory() ? "[dir] " : "") << entry.path().filename().string() << '\n';
std::vector<std::filesystem::directory_entry> listFolder(const std::filesystem::path& folder)
{
    std::vector<std::filesystem::directory_entry> entries;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(folder))
    {
        entries.push_back(entry);
    }
    std::ranges::sort(entries);
    return entries;
}
