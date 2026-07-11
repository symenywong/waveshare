idf_component_register(
    SRCS
        "src/aiqa_net_connect.c"
        "src/aiqa_net_policy.c"
    INCLUDE_DIRS "include"
    REQUIRES config_store esp_event esp_netif esp_wifi freertos log lwip
)
