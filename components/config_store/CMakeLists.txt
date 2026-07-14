idf_component_register(
    SRCS
        "src/aiqa_config.c"
        "src/aiqa_config_transaction.c"
        "src/aiqa_config_nvs.c"
        "src/aiqa_config_nvs_ab.c"
    INCLUDE_DIRS "include"
    REQUIRES app_core nvs_flash provider_common
)
