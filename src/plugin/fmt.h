#pragma once

#include <stdbool.h>
#include <deadbeef/deadbeef.h>
#include "../sfmt.h"

bool waveformat_is_valid(const ddb_waveformat_t *fmt);

size_t waveformat_buf_frames(
    const ddb_waveformat_t *fmt,
    size_t count);

size_t waveformat_frame_bytes_n(
    const ddb_waveformat_t *fmt,
    size_t count);

SFMT sfmt_from_waveformat(const ddb_waveformat_t *wf);
bool sfmt_apply_to_waveformat(ddb_waveformat_t *wf, SFMT fmt);
