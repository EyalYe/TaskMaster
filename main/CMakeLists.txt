# Firmware entry component — a trivial stub that calls taskmaster_run() (all bootstrap
# lives in taskmaster_core). Apps come from main/idf_component.yml; core from a git dep
# (template) or components/ (this repo).
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES taskmaster_core
)
