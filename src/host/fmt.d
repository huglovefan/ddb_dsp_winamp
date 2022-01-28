module ddw.host.fmt;

private extern (C) void _d_assertp(immutable(char)* file, uint line);

// -----------------------------------------------------------------------------

struct Fmt
{
	uint rate;
	uint bps;
	uint ch;
}

// -----------------------------------------------------------------------------

size_t fmt_frame_size(const(Fmt)* self) pure nothrow @nogc
{
	return (self.bps >> 3) * self.ch;
}

// -----------------------------------------------------------------------------

size_t fmt_frames2bytes(const(Fmt)* self, uint frames) pure nothrow @nogc
{
	return (self.bps >> 3) * self.ch * frames;
}

// -----------------------------------------------------------------------------

extern (C) uint fmt_bytes2frames(/*4[ESP]*/const(Fmt)*, /*8[ESP]*/size_t) pure nothrow @nogc
{
	asm pure nothrow @nogc
	{
		naked;
		mov EDX, 4[ESP];
		mov EAX, 8[ESP];
		mov ECX, Fmt.bps.offsetof[EDX];
		shr ECX, 3;
		imul ECX, Fmt.ch.offsetof[EDX];
		xor EDX, EDX;
		div ECX;
		test EDX, EDX;
		jne fail;
		ret;
fail:
		push 46; // line number
		push filename;
		call _d_assertp;
		int 3;
filename:
		db "fmt.d\0";
	}
}

unittest
{
	import core.exception;
	import std.exception;

	Fmt fmt = {ch: 2, bps: 16};
	assert(fmt_bytes2frames(&fmt, 3*(16/8)*2) == 3);

	assert(fmt_bytes2frames(&fmt, 100_000*(16/8)*2) == 100_000);
	assert(fmt_bytes2frames(&fmt, 100_000_000*(16/8)*2) == 100_000_000);

	assert(fmt_bytes2frames(&fmt, 0) == 0);

	// remainder
	assertNotThrown!AssertError(fmt_bytes2frames(&fmt, 0+3*(16/8)*2));
	assertThrown!AssertError(   fmt_bytes2frames(&fmt, 1+3*(16/8)*2));
	assertThrown!AssertError(   fmt_bytes2frames(&fmt, 2+3*(16/8)*2));
	assertThrown!AssertError(   fmt_bytes2frames(&fmt, 3+3*(16/8)*2));
	assertNotThrown!AssertError(fmt_bytes2frames(&fmt, 4+3*(16/8)*2));
}

// -----------------------------------------------------------------------------

/// version of fmt_bytes2frames without the remainder check
uint fmt_bytes2frames_approx(const(Fmt)* self, size_t bytes) pure nothrow @nogc
{
	return bytes / ((self.bps >> 3) * self.ch);
}

// -----------------------------------------------------------------------------

bool fmt_size_is_aligned(const(Fmt)* self, size_t bytes) pure nothrow @nogc
{
	return (bytes % ((self.bps >> 3) * self.ch)) == 0;
}

// -----------------------------------------------------------------------------

bool fmt_same(const(Fmt)* self, const(Fmt)* other) pure nothrow @nogc
{
	uint diff = 0;

	diff |= self.rate ^ other.rate;
	diff |= self.bps ^ other.bps;
	diff |= self.ch ^ other.ch;

	return diff == 0;
}

// -----------------------------------------------------------------------------

bool fmt_makes_sense(const(Fmt)* self) pure nothrow @nogc
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
