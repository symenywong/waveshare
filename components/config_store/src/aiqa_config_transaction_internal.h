#pragma once

#include <stdbool.h>

bool aiqa_config_lifecycle_try_lock(void);
void aiqa_config_lifecycle_unlock(void);
