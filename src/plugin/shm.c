#define _POSIX_C_SOURCE 200809L /* O_CLOEXEC */
#include "shm.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

void *shmnew(const char *path, size_t sz)
{
	void *p;
	int fd;

	p = MAP_FAILED;
	fd = open(path, O_RDWR|O_CREAT|O_EXCL|O_CLOEXEC, 0600);

	if (fd < 0)
	{
		fprintf(stderr,
		    "shmnew: open '%s': error %d\n", path, errno);
		goto end;
	}

	if (ftruncate(fd, sz) < 0)
	{
		fprintf(stderr,
		    "shmnew: ftruncate '%s': error %d\n", path, errno);
		goto end;
	}

	p = mmap(NULL, sz, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);

	if (p == MAP_FAILED)
	{
		fprintf(stderr,
		    "shmnew: mmap '%s': error %d\n", path, errno);
		goto end;
	}

end:

	if (fd >= 0)
		close(fd);

	if (p == MAP_FAILED)
	{
		unlink(path);
		p = NULL;
	}

	return p;
}

void shmfree(void *p, size_t sz)
{
	munmap(p, sz);
}
