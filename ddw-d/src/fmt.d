module ddw.fmt;

import ddw.zzx_deadbeef;

private extern (C) void _d_assertp(immutable(char)* file, uint line);

// -----------------------------------------------------------------------------

size_t fmt_frame_size(const(ddb_waveformat_t)* fmt) pure
{
	return fmt.channels * (fmt.bps >> 3);
}

// -----------------------------------------------------------------------------

size_t fmt_frames2bytes(/*RSI*/const(ddb_waveformat_t)*, /*EDI*/uint) pure
{
	asm pure
	{
		naked;
		mov EAX, dword ptr ddb_waveformat_t.bps.offsetof[RSI];
		shr EAX, 3;
		mul EAX, dword ptr ddb_waveformat_t.channels.offsetof[RSI];
		mul RDI;
		ret;
	}
}

unittest
{
	ddb_waveformat_t fmt = {channels: 2, bps: 16};

	assert(fmt_frames2bytes(&fmt, 0) == 0);
	assert(fmt_frames2bytes(&fmt, 1) == 1*(2*(16/8)));
	assert(fmt_frames2bytes(&fmt, 123) == 123*(2*(16/8)));

	// biggest possible result with a valid fmt
	fmt.channels = 8;
	fmt.bps = 32;
	assert(fmt_frames2bytes(&fmt, uint.max) == 137438953440);
}

// -----------------------------------------------------------------------------

uint fmt_bytes2frames(/*RSI*/const(ddb_waveformat_t)*, /*RDI*/size_t) pure
{
	asm pure
	{
		naked;

		// get frame size
		mov EAX, dword ptr ddb_waveformat_t.bps.offsetof[RSI];
		shr EAX, 3;
		mul EAX, dword ptr ddb_waveformat_t.channels.offsetof[RSI];

		// move frame size to RCX (since division uses RAX)
		mov RCX, RAX;

		// divide bytes (RDI) by frame size (RCX)
		xor RDX, RDX;
		mov RAX, RDI;
		div RCX;
		// -> buffer size (RAX)
		// -> division remainder (RDX)

		// remainder non-zero -> fail
		test RDX, RDX;
		jne fail;

		// result above uint.max -> fail
		mov RCX, 0xffffffff;
		cmp RAX, RCX;
		ja fail;

		ret;
fail:
		mov ESI, 77; // line number
		lea RDI, filename;
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

	ddb_waveformat_t fmt = {channels: 2, bps: 16};
	assert(fmt_bytes2frames(&fmt, 3*(16/8)*2) == 3);

	assert(fmt_bytes2frames(&fmt, 100_000*(16/8)*2) == 100_000);

	assert(fmt_bytes2frames(&fmt, 0) == 0);

	// remainder
	assertThrown!AssertError(fmt_bytes2frames(&fmt, 1+3*(16/8)*2));

	// overflow
	// this would be size_t.max/4 = 4611686018427387903
	assertThrown!AssertError(fmt_bytes2frames(&fmt, size_t.max));
}

// -----------------------------------------------------------------------------

bool fmt_is_reasonable(const(ddb_waveformat_t)* fmt) pure
{
	uint error = 0;

	// must be 8, 16, 24 or 32
	error |= cast(uint)fmt.bps-8 > 32-8;
	error |= fmt.bps & 7;

	// alsa supports 1-8
	error |= cast(uint)fmt.channels-1 & ~0b111;

	// reasonable range: 8000-192000
	error |= cast(uint)fmt.samplerate-8000 > 192000-8000;

	// bitmask of DDB_SPEAKER_* enum values in deadbeef.h (18 values defined)
	// count of set bits should be equal to fmt.channels
	enum EIGHTEEN_ONES = 0b111111111111111111;
	error |= fmt.channelmask & ~EIGHTEEN_ONES;

	// must be 0 or 1 (pcm_convert depends on this)
	error |= fmt.is_float & ~1;

	// must be 0
	error |= fmt.is_bigendian;

	// if float, bps must be 32
	error |= fmt.is_float * (fmt.bps ^ 32);

	uint cnt;
	asm pure
	{
		mov RBX, fmt;
		popcnt EAX, dword ptr fmt.channelmask.offsetof[RBX];
		mov cnt, EAX;
	}
	error |= cnt ^ fmt.channels;

	return error == 0;
}

unittest
{
	ddb_waveformat_t fmt0 = {
		bps: 16,
		channels: 2,
		samplerate: 44100,
		channelmask: 0b0011,
		is_float: 0,
		is_bigendian: 0,
	};
	assert(fmt_is_reasonable(&fmt0));

	static uint getbits(size_t n)
	{
		uint rv = 0;
		if (n > 32) n = 32;
		foreach (i; 0..n)
			rv |= 1<<i;
		return rv;
	}

	{
		auto fmt = fmt0;
		foreach (bps; -1..32*2+1) // bps
		{
			fmt.bps = bps;
			assert((bps == 8 || bps == 16 || bps == 24 || bps == 32) == fmt_is_reasonable(&fmt));
		}
	}
	{
		auto fmt = fmt0;
		foreach (channels; -1..8*2+1) // channels
		{
			fmt.channels = channels;
			fmt.channelmask = getbits(channels);
			assert((channels >= 1 && channels <= 8) == fmt_is_reasonable(&fmt));
		}
		foreach (channels; -1..8*2+1) // channels, with wrong channel mask (random bit)
		{
			fmt.channels = channels;
			fmt.channelmask = getbits(channels) ^ 0b00100;
			assert(!fmt_is_reasonable(&fmt));
		}
		foreach (channels; -1..8*2+1) // channels, with wrong channel mask (0)
		{
			fmt.channels = channels;
			fmt.channelmask = 0;
			assert(!fmt_is_reasonable(&fmt));
		}
		{ // bad channelmask (bits above 18 set)
			fmt.channels = 2;
			fmt.channelmask = 0b11000000000000000000;
			assert(!fmt_is_reasonable(&fmt));
		}
	}
	{
		auto fmt = fmt0;
		foreach (samplerate; [0, 7999, 192001, -1])
		{
			fmt.samplerate = samplerate;
			assert(!fmt_is_reasonable(&fmt));
		}
	}
	{
		auto fmt = fmt0;
		foreach (is_float; [2, 3, -1])
		{
			fmt.bps = 32;
			fmt.is_float = is_float;
			assert(!fmt_is_reasonable(&fmt));
		}
		foreach (bps; [8, 16, 24, 33, 64])
		{
			fmt.bps = bps;
			fmt.is_float = 1;
			assert(!fmt_is_reasonable(&fmt));
		}
		foreach (bps; [32])
		{
			fmt.bps = bps;
			fmt.is_float = 1;
			assert(fmt_is_reasonable(&fmt));
		}
	}
	{
		auto fmt = fmt0;
		foreach (is_bigendian; [1, 2, 3, -1])
		{
			fmt.is_bigendian = is_bigendian;
			assert(!fmt_is_reasonable(&fmt));
		}
	}
}

// -----------------------------------------------------------------------------

bool fmt_same(const(ddb_waveformat_t)* fmt1, const(ddb_waveformat_t)* fmt2)
{
	return *fmt1 == *fmt2;
}
