#pragma once

#if defined(__cplusplus)
extern "C" {
#endif

double monotime(void);

#define monotime_sec() (monotime())
#define monotime_msec() (monotime()*1000.0)

#if defined(__cplusplus)
}
#endif
