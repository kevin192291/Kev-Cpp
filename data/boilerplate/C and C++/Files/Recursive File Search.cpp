---
order: 70
description: findFiles(folder, ".cpp") lists matching files in all subfolders, skipping hidden and unreadable ones
includes: <filesystem> <string> <string_view> <vector>
---
// findFiles("src", ".cpp") gives every .cpp file under src. Hidden folders (.git...) aren't entered and
// folders it may not read are skipped instead of throwing.
std::vector<std::filesystem::path> findFiles(const std::filesystem::path& folder, std::string_view extension)
{
    namespace fs = std::filesystem;
    std::vector<fs::path> found;
    auto it = fs::recursive_directory_iterator(folder, fs::directory_options::skip_permission_denied);
    for (; it != fs::recursive_directory_iterator(); ++it)
    {
        if (it->is_directory() && it->path().filename().string().starts_with('.'))
        {
            it.disable_recursion_pending();
        }
        else if (it->is_regular_file() && it->path().extension() == extension)
        {
            found.push_back(it->path());
        }
    }
    return found;
}
