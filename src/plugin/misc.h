#pragma once

#include <stdbool.h>
#include <stddef.h>

struct iovec;

bool read_full(int fd, void *data, size_t size);
bool writev_full(int fd, struct iovec *iovecs, size_t count);
