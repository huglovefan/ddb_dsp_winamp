#pragma once

#include <stdbool.h>

void
tickthread_start_ticking(void);
void
tickthread_stop_ticking(void);

bool
tickthread_init(void);
void
tickthread_deinit(void);
