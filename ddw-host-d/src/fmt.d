module ddw.host.fmt;

nothrow:
@nogc:

// -----------------------------------------------------------------------------

struct Fmt
{
	uint rate;
	uint bps;
	uint ch;
}

// -----------------------------------------------------------------------------

size_t fmt_frame_size(const(Fmt)* self)
{
	return (self.bps>>3)*self.ch;
}

size_t fmt_frames2bytes(const(Fmt)* self, uint frames)
{
	return frames*fmt_frame_size(self);
}

uint fmt_bytes2frames(const(Fmt)* self, size_t bytes)
{
	return cast(uint)(bytes/fmt_frame_size(self));
}

bool fmt_same(const(Fmt)* self, const(Fmt)* other)
{
	uint diff = 0;

	diff |= self.rate ^ other.rate;
	diff |= self.bps ^ other.bps;
	diff |= self.ch ^ other.ch;

	return diff == 0;
}

bool fmt_makes_sense(const(Fmt)* self)
{
	uint err = 0;

	err |= self.rate < 8000;
	err |= self.rate > 192000;

	err |= self.bps == 0;
	err |= self.bps & 7; // not a multiple of 8
	err |= self.bps & cast(uint)~63; // bits above 32 set

	err |= self.ch < 1;
	err |= self.ch > 8;

	return err == 0;
}
