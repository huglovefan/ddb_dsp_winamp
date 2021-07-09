#include "shm.h"

#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

void *
shmnew(const char *path, size_t sz)
{
	int fd;
	void *p = NULL;

	fd = open(path, O_RDWR|O_CREAT|O_EXCL, 0600);
	if (fd == -1) {
		perror("shmnew: open");
		goto out;
	}

	if (ftruncate(fd, sz) == -1) {
		perror("shmnew: ftruncate");
		goto out;
	}

	p = mmap(NULL, sz, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (p == NULL) {
		perror("shmnew: mmap");
		goto out;
	}
out:
	if (fd != -1)
		close(fd);

	return p;
}

void
shmfree(void *p, size_t sz)
{
	munmap(p, sz);
}
