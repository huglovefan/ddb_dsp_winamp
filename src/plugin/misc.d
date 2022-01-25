import core.sys.posix.unistd;

bool read_full(int fd, void* data, size_t size)
{
	ssize_t result = read(fd, data, size);
	return result >= 0 && cast(size_t)result == size;
}

bool write_full(int fd, const(void)* data, size_t size)
{
	ssize_t result = write(fd, data, size);
	return result >= 0 && cast(size_t)result == size;
}

bool read_full(int fd, void[] data)
{
	ssize_t result = read(fd, data.ptr, data.length);
	return result >= 0 && cast(size_t)result == data.length;
}

bool write_full(int fd, const(void)[] data)
{
	ssize_t result = write(fd, data.ptr, data.length);
	return result >= 0 && cast(size_t)result == data.length;
}
