# app_hello — a self-registering app component.
# WHOLE_ARCHIVE keeps the registration constructor from being dropped by --gc-sections
# (nothing references this component's symbols directly; it registers itself at boot).
idf_component_register(
    SRCS "app_hello.c"
    REQUIRES taskmaster_core
    WHOLE_ARCHIVE
)
