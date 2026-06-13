#pragma once

#include "buf.hpp"
#include "../afmt.h"

struct Plugin;

/*
Test if we're able to convert audio from afmt to bfmt.
*/
bool can_convert_fmts(const AFMT *afmt, const AFMT *bfmt);

/*
Convert the buffer to a suitable format for processing by the plugin.

- buf holds the audio to be converted. Its format is given by fmt.
- tmpbuf may be used as a temporary buffer during conversion. Its
  contents may be destroyed, and its internal allocation may be swapped
  with buf's.
- pl is the plugin that we're converting samples for. The conversion
  takes into account any format restrictions given in its options.

On success, returns true. buf and fmt now have a format that can be
processed by the plugin. The contents of tmpbuf are unspecified.

The conversion will preserve any reserved space in buf. The size is
scaled to fit the same number of audio frames in the new format.

On failure, returns false. This will only happen if we can't convert
between the two formats. The buffers and the format will be unmodified
in this case.

To tell in advance if the conversion will work, use can_convert_fmts.

It is an error to call this function with an empty buffer, or with an
invalid format. The two buffers must not be the same.
*/
bool conv_buf_for_plugin(
    Buf           *buf,
    AFMT          *fmt,
    Buf           *tmpbuf,
    struct Plugin *pl);

/*
Convert the two buffers to the same suitable format for processing by
the plugin.

This is essentially a two-buffer version of conv_buf_for_plugin.
*/
bool conv_bufs_same_for_plugin(
    Buf           *a,
    AFMT          *afmt,
    Buf           *b,
    AFMT          *bfmt,
    Buf           *tmp,
    struct Plugin *pl);

/*
Low-level audio conversion function.

- buf holds the audio to be converted. Its format is given by from.
- The desired output format is specified by to.
- tmpbuf may be used as a temporary buffer during conversion. Its
  contents may be destroyed, and its internal allocation may be swapped
  with buf's.

On success, returns true. The audio data in buf is now in the format
specified by to. The contents of tmpbuf are unspecified.

The conversion will preserve any reserved space in buf. The size is
scaled to fit the same number of audio frames in the new format.

On failure, returns false. This will only happen if we can't convert
between the two formats. The two buffers will be unmodified in this
case.

To tell in advance if the conversion will work, use can_convert_fmts.

It is an error to call this function with an empty buffer, or with an
invalid format. The two buffers must not be the same.
*/
bool buf_convert_audio(
    Buf        *buf,
    Buf        *tmpbuf,
    const AFMT *from,
    const AFMT *to);
