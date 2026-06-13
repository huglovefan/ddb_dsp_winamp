#pragma once

#include "../afmt.h"
#include "buf.hpp"
#include "main.hpp"
#include "plugin.hpp"

void plugin_process_all(
    struct plugin_list *plugins,
    AFMT               &fmt,
    struct Buf         &data,
    struct Buf         &tmp);

unsigned int processable_size(
    const struct Plugin *pl,
    unsigned int         frames_avail);
