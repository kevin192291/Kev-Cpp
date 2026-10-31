#include "core/Version.h"

#include <nlohmann/json.hpp>

#include <ctime>
#include <regex>

namespace fs = std::filesystem;

namespace kevc {

std::string versionText(int64_t time, std::string_view commit) {
    const std::time_t t = static_cast<std::time_t>(time);
    std::tm utc{};
    gmtime_r(&t, &utc);
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%Y.%m.%d.%H%M", &utc);
    return std::string(stamp) + "-" + std::string(commit);
}

std::optional<Version> parseVersion(std::string_view text) {
    static const std::regex re(R"(^v?(\d{4})\.(\d{2})\.(\d{2})\.(\d{2})(\d{2})-([0-9a-f]{7,40})(\+.*)?$)");
    std::match_results<std::string_view::const_iterator> m;
    if (!std::regex_match(text.begin(), text.end(), m, re)) return std::nullopt;
    std::tm utc{};
    utc.tm_year = std::stoi(m[1]) - 1900;
    utc.tm_mon = std::stoi(m[2]) - 1;
    utc.tm_mday = std::stoi(m[3]);
    utc.tm_hour = std::stoi(m[4]);
    utc.tm_min = std::stoi(m[5]);
    const std::time_t time = timegm(&utc);
    if (time <= 0) return std::nullopt;
    std::string shown(text);
    if (shown.starts_with("v")) shown.erase(0, 1);
    return Version{static_cast<int64_t>(time), m[6].str(), shown};
}

std::string latestReleaseUrl(std::string_view repository) {
    return "https://api.github.com/repos/" + std::string(repository) + "/releases/latest";
}

std::optional<Release> parseLatestRelease(std::string_view text) {
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(text);
    } catch (const std::exception&) {
        return std::nullopt;
    }
    if (!j.is_object()) return std::nullopt;
    Release r;
    r.tag = j.value("tag_name", "");
    auto version = parseVersion(r.tag);
    if (!version) return std::nullopt;
    r.version = *version;
    r.title = j.value("name", r.tag);
    r.notes = j.value("body", "");
    r.page = j.value("html_url", "");
    const nlohmann::json assets = j.value("assets", nlohmann::json::array());
    for (const auto& a : assets) {
        const std::string name = a.value("name", "");
        if (!name.ends_with("-linux-x86_64.tar.gz")) continue;
        r.assetName = name;
        r.assetUrl = a.value("browser_download_url", "");
        r.assetSize = a.value("size", int64_t{0});
        break;
    }
    if (r.assetUrl.empty()) return std::nullopt;
    return r;
}

Freshness compareVersions(const Version& running, const Version& latest) {
    // A build without a version (made outside git) is treated as older than any release.
    if (!running.known() || running.time < latest.time) return Freshness::UpdateAvailable;
    if (running.time > latest.time) return Freshness::AheadOfRelease;
    return Freshness::UpToDate;
}

std::optional<fs::path> installPrefix(const fs::path& executable) {
    const fs::path bin = executable.parent_path();
    if (bin.filename() != "bin") return std::nullopt;
    return bin.parent_path();
}

std::vector<std::string> updateCommand(const Release& release, const fs::path& cacheDir, const fs::path& prefix) {
    // set -e: stop at the first failure; the terminal shows which step it was.
    static const char* script = R"sh(set -e
url=$1 archive=$2 dir=$3 prefix=$4
echo "Downloading $url"
mkdir -p "$(dirname "$archive")"
curl -fL --progress-bar -o "$archive" "$url"
rm -rf "$dir"
mkdir -p "$dir"
tar -xzf "$archive" -C "$dir" --strip-components=1
exec "$dir/install.sh" --prefix "$prefix"
)sh";
    const fs::path archive = cacheDir / release.assetName;
    const fs::path dir = cacheDir / ("kev-cpp-" + release.version.text);
    return {"sh", "-c", script, "kev-update", release.assetUrl, archive.string(), dir.string(), prefix.string()};
}

} // namespace kevc
