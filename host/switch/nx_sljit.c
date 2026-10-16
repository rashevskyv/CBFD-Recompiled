/* Executable memory for sljit (nx_sljit_config.h). Not done yet: mods' code (LiveRecomp) needs
   libnx's Jit (a writable view and an executable view of the same memory, the offset between them
   being SLJIT_EXEC_OFFSET). Until then it fails cleanly, as an out-of-memory. */

#include <stdio.h>

#include "nx_sljit_config.h"

void* conker_nx_malloc_exec(size_t size) {
    fprintf(stderr, "[mods] no executable memory on the Switch yet (0x%zX bytes asked)\n", size);
    return NULL;
}

void conker_nx_free_exec(void* ptr) {
    (void)ptr;
}

long conker_nx_exec_offset(void* ptr) {
    (void)ptr;
    return 0;
}
