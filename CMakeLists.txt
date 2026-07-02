# TaskMaster-C3 — top-level ESP-IDF project file
cmake_minimum_required(VERSION 3.16)

# Phase 6 §6E step 1 — regenerate the app manifest from the user-owned apps.yaml
# BEFORE the component manager (invoked by project.cmake) resolves dependencies.
# apps.yaml is the only app list you edit; main/idf_component.yml is a generated,
# git-ignored artifact. Runs on every configure; rewrites only when apps.yaml changed.
# Register apps.yaml as a configure dependency so editing it forces a reconfigure
# (→ recompose) — otherwise a plain `idf.py build` wouldn't pick up the change.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/apps.yaml")
find_package(Python3 QUIET COMPONENTS Interpreter)
if(NOT Python3_EXECUTABLE)
    set(Python3_EXECUTABLE python3)
endif()
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/tools/compose_apps.py"
    RESULT_VARIABLE _compose_rc)
if(NOT _compose_rc EQUAL 0)
    message(FATAL_ERROR "compose_apps.py failed — check apps.yaml")
endif()

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
