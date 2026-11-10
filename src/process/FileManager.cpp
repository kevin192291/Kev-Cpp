#include "process/FileManager.h"

#include <giomm.h>

#include <vector>

namespace kevc {

namespace {

void openWithDesktop(const std::filesystem::path& folder, const FileManagerError& onError) {
    try {
        Gio::AppInfo::launch_default_for_uri(Glib::filename_to_uri(folder.string()));
    } catch (const Glib::Error& e) {
        if (onError) onError(e.what());
    }
}

// `method` is ShowItems (selects each file in its folder) or ShowFolders.
void askFileManager(const char* method, const std::filesystem::path& path, const std::filesystem::path& fallback,
                    FileManagerError onError) {
    try {
        const auto bus = Gio::DBus::Connection::get_sync(Gio::DBus::BusType::SESSION);
        const std::vector<Glib::ustring> uris{Glib::filename_to_uri(path.string())};
        // The second argument is a startup notification id, which we don't have.
        const auto parameters = Glib::VariantContainerBase::create_tuple(
            {Glib::Variant<std::vector<Glib::ustring>>::create(uris), Glib::Variant<Glib::ustring>::create("")});
        // Nothing owns the name and nothing can be started for it: the call fails and the fallback runs.
        bus->call(
            "/org/freedesktop/FileManager1", "org.freedesktop.FileManager1", method, parameters,
            [bus, fallback, onError](const Glib::RefPtr<Gio::AsyncResult>& result) {
                try {
                    bus->call_finish(result);
                } catch (const Glib::Error&) {
                    openWithDesktop(fallback, onError);
                }
            },
            "org.freedesktop.FileManager1");
    } catch (const Glib::Error&) {
        openWithDesktop(fallback, onError); // no session bus
    }
}

} // namespace

void showInFileManager(const std::filesystem::path& file, FileManagerError onError) {
    askFileManager("ShowItems", file, file.parent_path(), std::move(onError));
}

void openInFileManager(const std::filesystem::path& folder, FileManagerError onError) {
    askFileManager("ShowFolders", folder, folder, std::move(onError));
}

} // namespace kevc
