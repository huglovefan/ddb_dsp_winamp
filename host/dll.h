#pragma once

#include <utility>

class Dll {
private:
	void *p;
public:
	Dll(const char *);
	~Dll();

	Dll(const Dll&) = delete;
	Dll& operator=(const Dll&) = delete;

	Dll(Dll&& other) noexcept;
	Dll& operator=(Dll&& other) noexcept {
		if (&other != this) {
			this->p = std::exchange(other.p, nullptr);
		}
		return *this;
	}

	void *getHandle();
	void *getProc(const char *name);
};
