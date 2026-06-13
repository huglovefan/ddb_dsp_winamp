#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include "../conv.h"

/* https://stackoverflow.com/a/323302 */
/* Robert Jenkins' 96 bit Mix Function */
static unsigned int mix(
	unsigned int a,
	unsigned int b,
	unsigned int c)
{
	a=a-b; a=a-c; a=a^(c >> 13);
	b=b-c; b=b-a; b=b^(a << 8);
	c=c-a; c=c-b; c=c^(b >> 13);
	a=a-b; a=a-c; a=a^(c >> 12);
	b=b-c; b=b-a; b=b^(a << 16);
	c=c-a; c=c-b; c=c^(b >> 5);
	a=a-b; a=a-c; a=a^(c >> 3);
	b=b-c; b=b-a; b=b^(a << 10);
	c=c-a; c=c-b; c=c^(b >> 15);
	return c;
}

static unsigned long long get_entropy(void)
{
	union {
		char b[8];
		unsigned long long u;
	} entropy;
	ssize_t readrv;
	int fd;

	fd = open("/dev/random", O_RDONLY);
	if (fd >= 0)
	{
		readrv = read(fd, entropy.b, sizeof(entropy));
		close(fd);
	}

	if (fd < 0 || readrv != sizeof(entropy))
	{
		entropy.u = mix(clock(), time(NULL), getpid());
		entropy.u <<= 32;
		entropy.u |= mix(
		    (uintptr_t)&get_entropy,
		    (uintptr_t)&entropy,
		    (uintptr_t)pthread_self());
	}

	return entropy.u;
}

int main(int argc, char **argv)
{
	int fromfmt;
	int tofmt;
	int i;
	unsigned long long entropy;

	fromfmt = SFMT_INVALID;
	tofmt = SFMT_INVALID;

	for (i = 1; i < argc; i++)
	{
		if (!strncmp(argv[i], "-fromfmt=", 9))
		{
			fromfmt = sfmt_parse(argv[i]+9);
			if (fromfmt == SFMT_INVALID)
			{
				fprintf(stderr,
				    "testconv: invalid input sample"
				    " format '%s'\n",
				    argv[i]+9);
				return 1;
			}
		}
		else if (!strncmp(argv[i], "-tofmt=", 7))
		{
			tofmt = sfmt_parse(argv[i]+7);
			if (tofmt == SFMT_INVALID)
			{
				fprintf(stderr,
				    "testconv: invalid output sample"
				    " format '%s'\n",
				    argv[i]+7);
				return 1;
			}
		}
		else
		{
			fprintf(stderr,
			    "testconv: unknown option '%s'\n",
			    argv[i]);
			return 1;
		}
	}

	if (fromfmt == SFMT_INVALID || tofmt == SFMT_INVALID)
	{
		fprintf(stderr,
		    "usage: testconv <options>\n"
		    "options:\n"
		    "  -fromfmt=<name>  input sample format (s8|s16|s24|s32|f32)\n"
		    "  -tofmt=<name>    output sample format (s8|s16|s24|s32|f32)\n"
		    "");
		return 1;
	}

	if (isatty(0) || isatty(1))
	{
		fprintf(stderr,
		    "testconv: stdin or stdout is a terminal,"
		    " exiting\n");
		return 1;
	}

	entropy = get_entropy();

	for (;;)
	{
		enum { BUF = 8*1024*1024 };
		static union { char p[BUF]; float align; } inbuf;
		static union { char p[BUF*4]; float align; } outbuf;
		ssize_t readrv;
		ssize_t writerv;

		readrv = read(0, inbuf.p, BUF - BUF%sfmt_bytes(fromfmt));
		if (readrv < 0)
		{
readerr:
			perror("testconv: read error");
			return 1;
		}
		else if (!readrv)
			break;
		else while (readrv % sfmt_bytes(fromfmt))
		/* if we read a partial sample, read a few more bytes to
		   complete it here. probably, it would be smarter to
		   just process the full samples and use the partial one
		   next time, but this was simpler to implement. */
		{
			ssize_t rv;
			ssize_t rem;

			rem =
			    sfmt_bytes(fromfmt)
			    - (readrv % sfmt_bytes(fromfmt));

			if (0)
				fprintf(stderr,
				    "read %zd bytes, need %zd more to"
				    " have a mult of %zu\n",
				    readrv,
				    rem,
				    sfmt_bytes(fromfmt));

			rv = read(0, inbuf.p+readrv, rem);
			if (rv < 0)
				goto readerr;
			else if (!rv)
			{
				fprintf(stderr,
				    "testconv: input data size is not a"
				    " multiple of sample size %zu"
				    " (wrong format?)\n",
				    sfmt_bytes(fromfmt));
				return 1;
			}
			readrv += rv;
		}

		conv_bits((struct conv_request){
		    .src = inbuf.p,
		    .dst = outbuf.p,
		    .count = readrv/sfmt_bytes(fromfmt),
		    .from = fromfmt,
		    .to = tofmt,
		    .entropy = entropy,
		});

		/* scale readrv to reuse it for writing */
		readrv /= sfmt_bytes(fromfmt);
		readrv *= sfmt_bytes(tofmt);

		writerv = write(1, outbuf.p, readrv);
		if (writerv < 0)
		{
writeerr:
			perror("testconv: write error");
			return 1;
		}
		else while (writerv < readrv)
		{
			ssize_t rv;

			rv = write(1, outbuf.p+writerv, readrv-writerv);
			if (rv < 0)
				goto writeerr;
			writerv += rv;
		}
	}

	return 0;
}
