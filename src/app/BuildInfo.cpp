// Kept apart from the rest of the app so a new commit only recompiles this file.
#include "app/BuildInfo.h"

#include "kevc_version.h"

#include <cstdlib>

namespace kevc {

Version buildVersion() {
    if (const char* pretend = std::getenv("KEVC_PRETEND_VERSION"); pretend && *pretend)
        if (auto v = parseVersion(pretend)) return *v;
    return Version{KEVC_BUILD_TIME, KEVC_BUILD_COMMIT, KEVC_BUILD_VERSION};
}

} // namespace kevc
