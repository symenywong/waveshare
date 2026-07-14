#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Starts the dedicated USB Serial/JTAG management owner task. */
esp_err_t aiqa_usb_management_start(void);

#ifdef __cplusplus
}
#endif
