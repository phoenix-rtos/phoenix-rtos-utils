#include "hal.h"
#include <string.h>


int hal_fillPrStatus(cpu_context_t *ctx, elf_prstatus *prstatus, __u32 memRecvPort, const coredump_opts_t *opts)
{
	prstatus->pr_reg.sepc = ctx->sepc;
	prstatus->pr_reg.ra = ctx->ra;
	prstatus->pr_reg.sp = ctx->sp;
	prstatus->pr_reg.gp = ctx->gp;
	prstatus->pr_reg.tp = ctx->tp;
	prstatus->pr_reg.t0 = ctx->t0;
	prstatus->pr_reg.t1 = ctx->t1;
	prstatus->pr_reg.t2 = ctx->t2;
	prstatus->pr_reg.s0 = ctx->s0;
	prstatus->pr_reg.s1 = ctx->s1;
	prstatus->pr_reg.a0 = ctx->a0;
	prstatus->pr_reg.a1 = ctx->a1;
	prstatus->pr_reg.a2 = ctx->a2;
	prstatus->pr_reg.a3 = ctx->a3;
	prstatus->pr_reg.a4 = ctx->a4;
	prstatus->pr_reg.a5 = ctx->a5;
	prstatus->pr_reg.a6 = ctx->a6;
	prstatus->pr_reg.a7 = ctx->a7;
	prstatus->pr_reg.s2 = ctx->s2;
	prstatus->pr_reg.s3 = ctx->s3;
	prstatus->pr_reg.s4 = ctx->s4;
	prstatus->pr_reg.s5 = ctx->s5;
	prstatus->pr_reg.s6 = ctx->s6;
	prstatus->pr_reg.s7 = ctx->s7;
	prstatus->pr_reg.s8 = ctx->s8;
	prstatus->pr_reg.s9 = ctx->s9;
	prstatus->pr_reg.s10 = ctx->s10;
	prstatus->pr_reg.s11 = ctx->s11;
	prstatus->pr_reg.t3 = ctx->t3;
	prstatus->pr_reg.t4 = ctx->t4;
	prstatus->pr_reg.t5 = ctx->t5;
	prstatus->pr_reg.t6 = ctx->t6;
	return 0;
}


void hal_createThreadAuxNotes(cpu_context_t *ctx, elf_thread_aux *note, const coredump_opts_t *opts)
{
	static const char FPREGSET_NAME[] = "CORE";
	if (opts->fpContext == 0) {
		return;
	}
	note->nhdr.n_namesz = sizeof(FPREGSET_NAME);
	note->nhdr.n_descsz = sizeof(ctx->fpCtx);
	note->nhdr.n_type = NT_FPREGSET;
	memcpy(note->name, FPREGSET_NAME, sizeof(FPREGSET_NAME));
	memcpy(&note->fpCtx, &ctx->fpCtx, sizeof(ctx->fpCtx));
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
