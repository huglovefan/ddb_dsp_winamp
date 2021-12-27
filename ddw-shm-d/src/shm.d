module ddw.shm.shm;

import core.stdc.stdio;
import core.stdc.string;
import core.sys.posix.fcntl;
import core.sys.posix.unistd;
import core.sys.posix.sys.mman;

enum o0600 = 384;

void* shmnew(const(char)* path, size_t sz)
{
	int fd;
	void* p = null;

	fd = open(path, O_RDWR|O_CREAT|O_EXCL, o0600);
	if (fd == -1) {
		perror("shmnew: open");
		goto Lout;
	}

	if (ftruncate(fd, sz) == -1) {
		perror("shmnew: ftruncate");
		goto Lout;
	}

	p = mmap(null, sz, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (p == null) {
		perror("shmnew: mmap");
		goto Lout;
	}
Lout:
	if (fd != -1)
		close(fd);

	return p;
}

void shmfree(void* p, size_t sz)
{
	munmap(p, sz);
}
