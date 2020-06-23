#include <stdexcept>

#if defined(_WIN32)
# include <windows.h>
#else
# include <dlfcn.h>
# define LoadLibrary(s)		dlopen(s, RTLD_LAZY)
# define FreeLibrary(p)		dlclose(p)
# define GetProcAddress(p, s)	dlsym(p, s)
typedef void *HMODULE;
#endif

#include "dll.h"

Dll::Dll(const char *path)
	: p(LoadLibrary(path)) {
	if (p == nullptr) {
		throw std::runtime_error("failed to load dll");
	}
}

Dll::~Dll() {
	if (p != nullptr) {
		FreeLibrary(reinterpret_cast<HMODULE>(p));
		p = nullptr;
	}
}

Dll::Dll(Dll&& other) noexcept
	: p(std::exchange(other.p, nullptr)) {
}

void *Dll::getHandle() {
	return reinterpret_cast<void *>(p);
}

void *Dll::getProc(const char *name) {
	return reinterpret_cast<void *>(GetProcAddress(reinterpret_cast<HMODULE>(p), name));
}
