#pragma once

#include "../afmt.h"

struct Buf;
struct Plugin;

/*
If supported, reset the plugin's internal buffers.

This can call ModifySamples, so it should only be used from the
processing thread.
*/
void plugin_process_reset(struct Plugin *pl, struct Buf *tmp);

/*
For processing code.
*/
void bufreset_single_step(
    struct Plugin *pl,
    const AFMT    *fmt,
    struct Buf    *tmp);

/*
For processing code.
*/
void bufreset_full(
    struct Plugin *pl,
    const AFMT    *fmt,
    struct Buf    *tmp);
