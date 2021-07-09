#pragma once

#include <stdint.h>
#include <unistd.h>

/* check if the values in a processing_request make sense */
#define PRREQ_IS_VALID(req) ( \
	(req).bitspersample != 0 && \
	(req).bitspersample % 8 == 0 && \
	(req).channels != 0 && \
	(req).buffer_size != 0 && \
	(req).buffer_size % (((req).bitspersample/8)*(req).channels) == 0 && \
	(req).samplerate != 0)

static bool
__attribute__((nonnull))
__attribute__((unused))
__attribute__((warn_unused_result))
read_full(int fd, void *data, size_t size)
{
	ssize_t result = read(fd, data, size);
	return result >= 0 && (size_t)result == size;
}

static bool
__attribute__((nonnull))
__attribute__((unused))
__attribute__((warn_unused_result))
write_full(int fd, const void *data, size_t size)
{
	ssize_t result = write(fd, data, size);
	return result >= 0 && (size_t)result == size;
}
