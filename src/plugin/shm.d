module ddw.plugin.shm;

import core.stdc.stdio;
import core.sys.posix.fcntl;
import core.sys.posix.sys.mman;
import core.sys.posix.unistd;

enum o600 = 384;

void* shmnew(const(char)* path, size_t sz)
{
	int fd = open(path, O_RDWR|O_CREAT|O_EXCL, o600);
	if (fd == -1)
	{
		perror("shmnew: open");
		return null;
	}

	if (ftruncate(fd, sz) != 0)
	{
		perror("shmnew: ftruncate");
		close(fd);
		unlink(path);
		return null;
	}

	void* p = mmap(null, sz, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (p == null)
	{
		perror("shmnew: mmap");
		close(fd);
		unlink(path);
		return null;
	}

	close(fd);

	return p;
}

void shmfree(void* p, size_t sz)
{
	munmap(p, sz);
}
