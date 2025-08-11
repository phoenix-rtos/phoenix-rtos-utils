#ifndef _HAL_H_
#define _HAL_H_

#include "cpu.h"
#include "settings.h"

#include <stdio.h>


#define PRINT_ERR(args...) fprintf(stderr, "Coredump server: " args)


typedef struct cpu_context_t cpu_context_t;

typedef struct elf_prstatus elf_prstatus;

typedef struct elf_thread_aux elf_thread_aux;

typedef struct elf_proc_aux elf_proc_aux;


extern int hal_fillPrStatus(cpu_context_t *ctx, elf_prstatus *prstatus, __u32 memRecvPort, const coredump_opts_t *opts);


extern void hal_createThreadAuxNotes(cpu_context_t *ctx, elf_thread_aux *buff, const coredump_opts_t *opts);


extern size_t hal_threadAuxNotesSize(const coredump_opts_t *opts);


extern void hal_createProcAuxNotes(elf_proc_aux *buff, const coredump_opts_t *opts);


extern size_t hal_procAuxNotesSize(const coredump_opts_t *opts);


extern void *hal_cpuGetUserSP(cpu_context_t *ctx);

#endif /* _HAL_H_ */
