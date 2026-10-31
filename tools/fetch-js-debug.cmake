# Downloads Microsoft's js-debug (the debugger VS Code uses for Node and Chrome) into tools/js-debug.
# It isn't published on npm, so the build fetches this pinned release and refuses it unless its
# SHA-256 matches. Kev-C++ runs js-debug/src/dapDebugServer.js to debug web projects (F5).
#   cmake -P tools/fetch-js-debug.cmake
set(version 1.140.0)
set(sha256 27dab92937ec1ab35821ae955aac867544fe06a1b6307229049f2d789af10968)
set(tools ${CMAKE_CURRENT_LIST_DIR})
set(stamp ${tools}/js-debug/.kev-cpp-${version})

if(EXISTS ${stamp})
  return()
endif()
set(archive ${tools}/js-debug-dap-v${version}.tar.gz)
file(DOWNLOAD
  https://github.com/microsoft/vscode-js-debug/releases/download/v${version}/js-debug-dap-v${version}.tar.gz
  ${archive}
  EXPECTED_HASH SHA256=${sha256}
  STATUS status
  TLS_VERIFY ON)
list(GET status 0 code)
if(NOT code EQUAL 0)
  file(REMOVE ${archive})
  list(GET status 1 message)
  message(WARNING "Could not download js-debug ${version} (${message}): debugging web projects will be unavailable")
  return()
endif()
file(REMOVE_RECURSE ${tools}/js-debug)
file(ARCHIVE_EXTRACT INPUT ${archive} DESTINATION ${tools}) # the archive holds js-debug/
file(REMOVE ${archive})
file(TOUCH ${stamp})
