/* Forced into sljit (LiveRecomp, the mods' recompiler) on the Switch: Horizon has no mmap, so
   sljit's own executable allocator can't build. Executable memory comes from nx_sljit.c instead. */
#ifndef CONKER_NX_SLJIT_CONFIG_H
#define CONKER_NX_SLJIT_CONFIG_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif
void* conker_nx_malloc_exec(size_t size);
void conker_nx_free_exec(void* ptr);
long conker_nx_exec_offset(void* ptr);
#ifdef __cplusplus
}
#endif

#define SLJIT_EXECUTABLE_ALLOCATOR 0
/* sljit's stack utilities allocate with malloc rather than mmap. */
#define SLJIT_UTIL_SIMPLE_STACK_ALLOCATION 1
#define SLJIT_MALLOC_EXEC(size, exec_allocator_data) conker_nx_malloc_exec(size)
#define SLJIT_FREE_EXEC(ptr, exec_allocator_data) conker_nx_free_exec(ptr)
#define SLJIT_EXEC_OFFSET(ptr) conker_nx_exec_offset(ptr)

#endif
