#pragma once

#include <stddef.h>

void *
shmnew(const char *path, size_t sz);

void
shmfree(void *p, size_t sz);
