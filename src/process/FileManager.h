#pragma once

#include <filesystem>
#include <functional>
#include <string>

namespace kevc {

// The desktop's file manager, for the Project tab's Open Containing Folder and Open This Folder.
// It's asked over D-Bus (org.freedesktop.FileManager1, which Nautilus, Nemo, Caja, Dolphin and Thunar
// answer), so a file comes up selected in its folder and a folder opens in a file manager even where
// something else (an editor) has claimed folders. Without one, the folder opens in whatever the
// desktop opens folders with. `onError` hears why neither worked; it can run after the call has
// returned, so it must check that its owner still exists.
using FileManagerError = std::function<void(const std::string& message)>;
void showInFileManager(const std::filesystem::path& file, FileManagerError onError);
void openInFileManager(const std::filesystem::path& folder, FileManagerError onError);

} // namespace kevc
