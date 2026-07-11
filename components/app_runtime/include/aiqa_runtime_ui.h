#pragma once

#include "aiqa_state_machine.h"
#include "board_wave_175c.h"

#ifdef __cplusplus
extern "C" {
#endif

board_wave_175c_display_page_t aiqa_runtime_ui_page_for(
    aiqa_state_t state,
    aiqa_error_code_t error);

#ifdef __cplusplus
}
#endif
