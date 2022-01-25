import core.sys.posix.fcntl;
import core.sys.posix.sys.mman;
import core.sys.posix.unistd;
import std.exception : errnoEnforce;
import std.string : toStringz;

enum o600 = 384;

void* shmnew(string path, size_t sz)
{
	int fd = open(path.toStringz, O_RDWR|O_CREAT|O_EXCL, o600);
	errnoEnforce(fd != -1);

	scope (exit)
		close(fd);

	scope (failure)
		unlink(path.toStringz);

	errnoEnforce(ftruncate(fd, sz) == 0);

	void* p = mmap(null, sz, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	errnoEnforce(p != null);

	return p;
}

void shmfree(void* p, size_t sz)
{
	errnoEnforce(munmap(p, sz) == 0);
}
