#include "hal.h"
#include <string.h>


int hal_fillPrStatus(cpu_context_t *ctx, elf_prstatus *prstatus, __u32 memRecvPort, const coredump_opts_t *opts)
{
	prstatus->pr_reg.r0 = ctx->r0;
	prstatus->pr_reg.r1 = ctx->r1;
	prstatus->pr_reg.r2 = ctx->r2;
	prstatus->pr_reg.r3 = ctx->r3;
	prstatus->pr_reg.r4 = ctx->r4;
	prstatus->pr_reg.r5 = ctx->r5;
	prstatus->pr_reg.r6 = ctx->r6;
	prstatus->pr_reg.r7 = ctx->r7;
	prstatus->pr_reg.r8 = ctx->r8;
	prstatus->pr_reg.r9 = ctx->r9;
	prstatus->pr_reg.r10 = ctx->r10;
	prstatus->pr_reg.fp = ctx->fp;
	prstatus->pr_reg.ip = ctx->ip;
	prstatus->pr_reg.sp = ctx->sp;
	prstatus->pr_reg.lr = ctx->lr;
	prstatus->pr_reg.pc = ctx->pc;
	prstatus->pr_reg.psr = ctx->psr;
	return 0;
}


void hal_createThreadAuxNotes(cpu_context_t *ctx, elf_thread_aux *note, const coredump_opts_t *opts)
{
	static const char ARMVFP_NAME[] = "LINUX";
	if (opts->fpContext == 0) {
		return;
	}
	note->nhdr.n_namesz = sizeof(ARMVFP_NAME);
	note->nhdr.n_descsz = sizeof(note->fpuContext);
	note->nhdr.n_type = NT_ARM_VFP;
	memcpy(note->name, ARMVFP_NAME, sizeof(ARMVFP_NAME));
	memcpy(note->fpuContext.freg, ctx->freg, sizeof(ctx->freg));
	note->fpuContext.fpsr = ctx->fpsr;
}


void hal_createProcAuxNotes(elf_proc_aux *note, const coredump_opts_t *opts)
{
	static const char AUXV_NAME[] = "CORE";
	if (opts->fpContext == 0) {
		return;
	}

	note->nhdr.n_namesz = sizeof(AUXV_NAME);
	note->nhdr.n_descsz = sizeof(note->auxv);
	note->nhdr.n_type = NT_AUXV;
	memcpy(note->name, AUXV_NAME, sizeof(AUXV_NAME));

	note->auxv[0].a_type = AT_HWCAP;
	note->auxv[0].a_val = HWCAP_VFPv3D16;
	note->auxv[1].a_type = AT_NULL;
	note->auxv[1].a_val = 0;
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
	if (opts->fpContext) {
		return sizeof(elf_proc_aux);
	}
	return 0;
}


void *hal_cpuGetUserSP(cpu_context_t *ctx)
{
	return (void *)ctx->sp;
}
