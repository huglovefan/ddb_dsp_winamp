#include "sfmt.h"

#include <assert.h>
#include <stdint.h>

bool sfmt_is_valid(SFMT fmt)
{
	return (fmt != SFMT_INVALID && fmt < SFMT_COUNT);
}

bool sfmt_equal(SFMT fmt1, SFMT fmt2)
{
	if (!sfmt_is_valid(fmt1))
		return false;
	if (!sfmt_is_valid(fmt2))
		return false;

	return (fmt1 == fmt2);
}

bool sfmt_is_float(SFMT fmt)
{
	return (fmt == SFMT_F32);
}

size_t sfmt_bytes(SFMT fmt)
{
	switch (fmt)
	{
	case SFMT_S8:  return 1;
	case SFMT_S16: return 2;
	case SFMT_S24: return 3;
	case SFMT_S32: return 4;
	case SFMT_F32: return 4;
	default:       return 0;
	}
}

size_t sfmt_bytes_n(SFMT fmt, size_t count)
{
	size_t n;

	n = sfmt_bytes(fmt);
	assert(count <= SIZE_MAX/n);
	return n*count;
}

const char *sfmt_name(SFMT fmt)
{
	switch (fmt)
	{
	case SFMT_S8:  return "s8";
	case SFMT_S16: return "s16";
	case SFMT_S24: return "s24";
	case SFMT_S32: return "s32";
	case SFMT_F32: return "f32";
	default:       return NULL;
	}
}

SFMT sfmt_parse(const char *s)
{
	/* :D */
	if (*s == 's')
	{
		if (s[1] == '8' && !s[2])
			return SFMT_S8;
		if (s[1] == '1' && s[2] == '6' && !s[3])
			return SFMT_S16;
		if (s[1] == '2' && s[2] == '4' && !s[3])
			return SFMT_S24;
		if (s[1] == '3' && s[2] == '2' && !s[3])
			return SFMT_S32;
	}
	if (*s == 'f')
	{
		if (s[1] == '3' && s[2] == '2' && !s[3])
			return SFMT_F32;
	}
	return SFMT_INVALID;
}
