#include <stdint.h>

struct __attribute__((__packed__)) processing_request {
	uint64_t buffer_size; /* how many bytes are written after this header */
	uint32_t samplerate;
	uint8_t bitspersample;
	uint8_t channels;
};

struct __attribute__((__packed__)) processing_response {
	uint64_t buffer_size; /* how many bytes are written after this header */
};

/* check if the values in a processing_request make sense */
#define PRREQ_IS_VALID(req) ( \
	(req).bitspersample != 0 && \
	(req).bitspersample % 8 == 0 && \
	(req).channels != 0 && \
	(req).buffer_size != 0 && \
	(req).buffer_size % (((req).bitspersample/8)*(req).channels) == 0 && \
	(req).samplerate != 0)

/*
 * other garbage
 */

/* none of this code is really performance-critical but i like using these */
#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
/* #define ASSUME(x) do { if (!(x)) __builtin_unreachable(); } while (0) */

struct abuffer {
	void *data;
	size_t size;
};

#define EMPTY_ABUFFER ((struct abuffer){.data = NULL, .size = 0})

static void
__attribute__((unused))
abuffer_xgrow(struct abuffer *self, size_t newsize)
{
	assert(self != NULL);
	assert(newsize > 0);

	if (UNLIKELY(self->size < newsize)) {
		void *newdata = realloc(self->data, newsize);
		assert(newdata != NULL);
		self->data = newdata;
		self->size = newsize;
	}
}

static void
__attribute__((unused))
abuffer_swap(struct abuffer *restrict ab1, struct abuffer *restrict ab2)
{
	struct abuffer tmp;

	assert(ab1 != NULL);
	assert(ab2 != NULL);
	assert(ab1 != ab2);

	tmp = *ab2;

	ab2->data = ab1->data;
	ab2->size = ab1->size;

	ab1->data = tmp.data;
	ab1->size = tmp.size;
}

static bool
__attribute__((nonnull))
__attribute__((unused))
__attribute__((warn_unused_result))
read_full(int fd, void *data, size_t size)
{
	ssize_t result = read(fd, data, size);
	return LIKELY(result >= 0 && (size_t)result == size);
}

static bool
__attribute__((nonnull))
__attribute__((unused))
__attribute__((warn_unused_result))
write_full(int fd, const void *data, size_t size)
{
	ssize_t result = write(fd, data, size);
	return LIKELY(result >= 0 && (size_t)result == size);
}
