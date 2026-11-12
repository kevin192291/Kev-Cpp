---
description: Copies assets/ next to the program on every build, so it also finds its files when run from there
ask: Target = ${PROJECT_NAME}
order: 60
section: install
---
# A target of its own rather than a POST_BUILD step, which only runs when the program is relinked:
# an edited image or sound would otherwise stay stale in the build folder.
add_custom_target({{Target}}_assets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/assets $<TARGET_FILE_DIR:{{Target}}>/assets
    COMMENT "Copying assets"
)
add_dependencies({{Target}} {{Target}}_assets)
