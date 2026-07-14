idf_component_register(
    SRCS
        "src/aiqa_management_protocol.c"
        "src/aiqa_management_wire.c"
        "src/aiqa_usb_management.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common esp_driver_usb_serial_jtag freertos heap json log
)
