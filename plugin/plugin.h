#pragma once

#include <deadbeef/deadbeef.h>

#include "child.h"

struct ddw {
	ddb_dsp_context_t ctx;
	char *dll;
	unsigned short max_bps;
	struct child host;
};

extern DB_functions_t *deadbeef;

enum {
	NEED_32BIT = 0b01,
	NEED_FLOAT = 0b10,
};

bool
ddw_has_dll(struct ddw *plugin);
