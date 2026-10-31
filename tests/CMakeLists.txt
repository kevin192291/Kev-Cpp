find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(kevc_tests
  BuildFilesTest.cpp
  BuildPlanTest.cpp
  DapTest.cpp
  DiagnosticsTest.cpp
  IconsTest.cpp
  LspTest.cpp
  ManagedBlockTest.cpp
  MiTest.cpp
  PackagesTest.cpp
  ProjectSessionTest.cpp
  SettingsTest.cpp
  SvelteKitTest.cpp
  ToolLocatorTest.cpp
  ToolchainTest.cpp
  VersionTest.cpp
  WebProjectTest.cpp
  WebReportsTest.cpp
)
target_link_libraries(kevc_tests PRIVATE kevc_core GTest::gtest_main)
target_compile_definitions(kevc_tests PRIVATE KEVC_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
gtest_discover_tests(kevc_tests)

find_program(NODE_EXECUTABLE node)
if(NODE_EXECUTABLE)
  add_test(NAME devserver COMMAND ${NODE_EXECUTABLE} --test ${CMAKE_SOURCE_DIR}/tools/devserver/devserver.test.mjs)
  add_test(NAME otel-collector COMMAND ${NODE_EXECUTABLE} --test ${CMAKE_SOURCE_DIR}/tools/otel/collector.test.mjs)
endif()
