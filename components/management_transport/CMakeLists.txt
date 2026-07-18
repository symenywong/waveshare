idf_component_register(
    SRCS
        "src/aiqa_management_protocol.c"
        "src/aiqa_management_wire.c"
        "src/aiqa_pairing_rpc.c"
        "src/aiqa_usb_management.c"
    INCLUDE_DIRS "include"
    REQUIRES app_runtime esp_common esp_driver_usb_serial_jtag freertos heap json log management_access management_service management_session management_session_esp
)
