idf_component_register(
    SRCS
        "src/aiqa_config.c"
        "src/aiqa_config_nvs.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash provider_common
)
