#include "hal.h"
#include <string.h>


int hal_fillPrStatus(cpu_context_t *ctx, elf_prstatus *prstatus, __u32 memRecvPort, const coredump_opts_t *opts)
{
	memcpy(prstatus->pr_reg.x, ctx->x, sizeof(prstatus->pr_reg.x));
	prstatus->pr_reg.sp = ctx->sp;
	prstatus->pr_reg.pc = ctx->pc;
	prstatus->pr_reg.psr = ctx->psr;
	return 0;
}


void hal_createThreadAuxNotes(cpu_context_t *ctx, elf_thread_aux *note, const coredump_opts_t *opts)
{
	static const char FPREGSET_NAME[] = "CORE";
	if (opts->fpContext == 0) {
		return;
	}
	note->nhdr.n_namesz = sizeof(FPREGSET_NAME);
	note->nhdr.n_descsz = sizeof(note->fpuContext);
	note->nhdr.n_type = NT_FPREGSET;
	memcpy(note->name, FPREGSET_NAME, sizeof(FPREGSET_NAME));

	memcpy(note->fpuContext.freg, &ctx->freg, sizeof(ctx->freg));
	note->fpuContext.fpsr = ctx->fpsr;
	note->fpuContext.fpcr = ctx->fpcr;
}


void hal_createProcAuxNotes(elf_proc_aux *buff, const coredump_opts_t *opts)
{
	return;
}


size_t hal_threadAuxNotesSize(const coredump_opts_t *opts)
{
	if (opts->fpContext) {
		return sizeof(elf_thread_aux);
	}
	return 0;
}


size_t hal_procAuxNotesSize(const coredump_opts_t *opts)
{
	return 0;
}


void *hal_cpuGetUserSP(cpu_context_t *ctx)
{
	return (void *)ctx->sp;
}
