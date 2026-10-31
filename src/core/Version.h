#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kevc {

// Kev-C++ versions are the commit a build was made from: its date and time (UTC) and short hash,
// "2026.10.31.1959-3fa834c". Releases are tagged "v" + that, so newer versions sort later.
struct Version {
    int64_t time = 0;   // the commit's time, seconds since 1970 (UTC); 0 if unknown
    std::string commit; // short hash
    std::string text;   // as shown to the user

    bool known() const { return time > 0; }
};

std::string versionText(int64_t time, std::string_view commit);
// "2026.10.31.1959-3fa834c", with or without a leading "v" or a "+changes" suffix.
std::optional<Version> parseVersion(std::string_view text);

// Where updates come from: the GitHub repository whose releases are Kev-C++ builds.
constexpr std::string_view kReleaseRepository = "kevin192291/Kev-Cpp";
std::string latestReleaseUrl(std::string_view repository = kReleaseRepository);

struct Release {
    Version version;
    std::string tag;
    std::string title;
    std::string notes;
    std::string page;      // the release's web page
    std::string assetName; // kev-cpp-<version>-linux-x86_64.tar.gz
    std::string assetUrl;
    int64_t assetSize = 0;
};
// GitHub's /releases/latest reply; nullopt if it isn't a Kev-C++ release with a Linux package.
std::optional<Release> parseLatestRelease(std::string_view json);

enum class Freshness { UpdateAvailable, UpToDate, AheadOfRelease };
Freshness compareVersions(const Version& running, const Version& latest);

// The prefix an installed kev-cpp lives under (<prefix>/bin/kev-cpp), or nullopt for a build tree.
std::optional<std::filesystem::path> installPrefix(const std::filesystem::path& executable);

// Downloads a release into `cacheDir`, unpacks it and runs its installer into `prefix`, as one
// command for a terminal (so the installer can ask for a sudo password if packages are missing).
std::vector<std::string> updateCommand(const Release& release, const std::filesystem::path& cacheDir,
                                       const std::filesystem::path& prefix);

} // namespace kevc
