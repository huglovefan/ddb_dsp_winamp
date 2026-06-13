#pragma once

struct _unittest_
{
	void (*fn)(void);
	const char *file;
	int line;
	struct _unittest_ *next;
};

extern struct _unittest_ *_unittest_head_;

#define _merge_(a, b, c) a##b##c

#define _merge_testfuncname_(a) _merge_(_unittest_L, a, _)
#define _merge_ctorfuncname_(a) _merge_(_ctor_L, a, _)
#define _merge_structname_(a) _merge_(_unitteststruct_L, a, _)

#define _testfuncname_ _merge_testfuncname_(__LINE__)
#define _ctorfuncname_ _merge_ctorfuncname_(__LINE__)
#define _structname_ _merge_structname_(__LINE__)

#define UNITTEST() \
static void _testfuncname_(void); \
static struct _unittest_ _structname_ = {&_testfuncname_,__FILE__,__LINE__}; \
__attribute__((constructor)) static void _ctorfuncname_(void) { \
	_structname_.next = _unittest_head_; \
	_unittest_head_ = &_structname_; \
} \
static void _testfuncname_(void)
