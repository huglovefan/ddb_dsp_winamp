module ddw.fmt;

import core.stdc.string;
import ddw.zzx_deadbeef;

size_t fmt_frame_size(const(ddb_waveformat_t)* fmt)
{
	return fmt.channels*(fmt.bps>>3);
}

size_t fmt_frames2bytes(const(ddb_waveformat_t)* fmt, uint frames)
{
	return frames*fmt_frame_size(fmt);
}

uint fmt_bytes2frames(const(ddb_waveformat_t)* fmt, size_t frames)
{
	return cast(uint)(frames/fmt_frame_size(fmt));
}

void fmt_assert_reasonable(const(ddb_waveformat_t)* fmt)
{
	// 8, 16, 24 or 32
	assert(fmt.bps >= 8 && fmt.bps <= 32 && fmt.bps % 8 == 0);

	// alsa supports 1-8
	assert(fmt.channels >= 1 && fmt.channels <= 8);

	// 8000 = mp3 minimum
	// 192000 = biggest one i've seen
	assert(fmt.samplerate >= 8000 && fmt.samplerate <= 192000);

	// bitmask. see DDB_SPEAKER_* in deadbeef.h
	// currently 18 values are defined
	// there should be one set for each channel (probably)
	// pcm_convert tolerates and converts wrong values here (i think)
enum EIGHTEEN_ONES = 0b111111111111111111;
	assert((fmt.channelmask&EIGHTEEN_ONES) != 0);
	assert((fmt.channelmask&~EIGHTEEN_ONES) == 0);

	// pcm_convert assumes this is 0 or 1
	assert(fmt.is_float == 0 || fmt.is_float == 1);

	// samples are little-endian on wintel
	assert(fmt.is_bigendian == 0);

	// deadbeef.h: "bps must be 32 if this is true"
	if (fmt.is_float)
		assert(fmt.is_float ? fmt.bps == 32 : 1);
}

bool fmt_same(const(ddb_waveformat_t)* fmt1, const(ddb_waveformat_t)* fmt2)
{
	return memcmp(fmt1, fmt2, ddb_waveformat_t.sizeof) == 0;
}

// test that it is ok to compare them with memcmp since the struct has no padding
unittest
{
	ddb_waveformat_t fmt1;
	ddb_waveformat_t fmt2;
	memset(&fmt1, 0b10101010, ddb_waveformat_t.sizeof);
	memset(&fmt2, 0b01010101, ddb_waveformat_t.sizeof);
	static void zeroall(ddb_waveformat_t* fmt)
	{
		fmt.bps = 0;
		fmt.channels = 0;
		fmt.samplerate = 0;
		fmt.channelmask = 0;
		fmt.is_float = 0;
		fmt.is_bigendian = 0;
	}
	zeroall(&fmt1);
	zeroall(&fmt2);
	assert(fmt_same(&fmt1, &fmt2));

	fmt2.bps += 1;
	assert(!fmt_same(&fmt1, &fmt2));
}
