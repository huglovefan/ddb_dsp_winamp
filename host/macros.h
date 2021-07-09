#pragma once

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define L(x) (likely(x))
#define U(x) (unlikely(x))

#define MAX(a_, b_) ({ typeof(a_) a = (a_), b = (b_); a < b ? b : a; })
#define MIN(a_, b_) ({ typeof(a_) a = (a_), b = (b_); a < b ? a : b; })

// the assert macro that comes with (mingw?) writes one character before it aborts

#undef assert

// https://stackoverflow.com/a/2671100
#define STRINGIZE_DETAIL(x) #x
#define STRINGIZE(x) STRINGIZE_DETAIL(x)

// need to use snprintf and write to reliably print it without mangling or truncation

__attribute__((cold, noinline, noipa, noreturn))
void
assert_fail(const char *fmt, const char *arg1, const char *arg2);

#define assert(x) \
	do { \
		if U (!(x)) assert_fail(__FILE__ ":" STRINGIZE(__LINE__) ": %s: Assertion failed: %s\n", __func__, STRINGIZE(x)); \
	} while (0)

#if defined(D)
 #undef D
 #define D if (1)
 #define UNITTEST(name) __attribute__((constructor)) static void name##_unittest(void)
#else
 #define D if (0)
 #define UNITTEST(name) __attribute__((unused)) static void name##_unittest(void)
#endif
