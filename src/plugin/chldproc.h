#pragma once

#include <deadbeef/deadbeef.h>
#include "child.h"
#include "../pipedata.h"

bool child_process_samples(
    struct Child     *self,
    void             *buf,
    size_t           *buflen_inout,
    size_t            buflen_max,
    ddb_waveformat_t *fmt_inout,
    SFMT_REQ          wantfmt);

bool child_inform_playback_interrupted(struct Child *self);
