# TaskMaster-C3 — top-level ESP-IDF project file
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(taskmaster_c3)

# Snapshot flashable artifacts of every successful build to build_backups/<timestamp>/.
# Hook the `app` target (not the .elf): it completes AFTER the .bin is generated.
# Best-effort (the script never fails the build).
idf_build_get_property(_proj_dir PROJECT_DIR)
add_custom_command(
    TARGET app
    POST_BUILD
    COMMAND bash ${_proj_dir}/tools/backup_build.sh
    VERBATIM
)
