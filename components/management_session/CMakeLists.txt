idf_component_register(
    SRCS
        "src/aiqa_pairing_crypto.c"
        "src/aiqa_secure_channel.c"
    INCLUDE_DIRS "include"
    REQUIRES mbedtls
)
