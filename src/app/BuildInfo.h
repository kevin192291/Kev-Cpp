#pragma once

#include "core/Version.h"

namespace kevc {

// This build's version: the commit it was made from (see cmake/version.cmake). The environment
// variable KEVC_PRETEND_VERSION (e.g. 2026.01.01.0000-0000000) overrides it, to test updating.
Version buildVersion();

} // namespace kevc
