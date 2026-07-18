idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES app_runtime management_access management_session_esp management_transport nvs_flash
)
