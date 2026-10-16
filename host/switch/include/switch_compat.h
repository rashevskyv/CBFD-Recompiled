/* Forced into every file of the Switch build (host/CMakeLists.txt): what newlib lacks and the
   libraries use. Defined in switch_compat.c. */
#ifndef CONKER_SWITCH_COMPAT_H
#define CONKER_SWITCH_COMPAT_H

#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* implot. */
time_t timegm(struct tm* tm);

#ifdef __cplusplus
}
#endif

#endif
