/* What newlib lacks (switch_compat.h). */

#include <errno.h>
#include <malloc.h>
#include <stdlib.h>

#include "include/switch_compat.h"

/* Horizon has no time zones: the C library's local time is UTC, so mktime is timegm. */
time_t timegm(struct tm* tm) {
    return mktime(tm);
}

/* newlib declares it but doesn't provide it (sse2neon's _mm_malloc uses it). Weak: mesa-switch's NVK
   brings its own. */
__attribute__((weak)) int posix_memalign(void** memptr, size_t alignment, size_t size) {
    void* memory = memalign(alignment, size);
    if (memory == NULL) {
        return ENOMEM;
    }
    *memptr = memory;
    return 0;
}

/* pthread_detach (std::thread::detach) calls this when it exists, and fails with ENOSYS
   otherwise, which makes detach() throw: the runtime's timer thread is detached. libnx has no
   detach, so the thread is simply never joined: its stack stays allocated after it ends (a few
   threads, for the program's whole life). */
struct __pthread_t;
int __syscall_thread_detach(struct __pthread_t* thread) {
    (void)thread;
    return 0;
}
