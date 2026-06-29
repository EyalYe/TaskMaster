# Firmware entry component. Apps come from main/idf_component.yml; core from components/.
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES taskmaster_core nvs_flash
)
