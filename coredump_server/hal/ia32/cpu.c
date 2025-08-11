#include "hal.h"
#include <string.h>


int hal_fillPrStatus(cpu_context_t *ctx, elf_prstatus *prstatus, __u32 memRecvPort, const coredump_opts_t *opts)
{
	prstatus->pr_reg.ebx = ctx->ebx;
	prstatus->pr_reg.ecx = ctx->ecx;
	prstatus->pr_reg.edx = ctx->edx;
	prstatus->pr_reg.esi = ctx->esi;
	prstatus->pr_reg.edi = ctx->edi;
	prstatus->pr_reg.ebp = ctx->ebp;
	prstatus->pr_reg.eax = ctx->eax;
	prstatus->pr_reg.ds = ctx->ds;
	prstatus->pr_reg.es = ctx->es;
	prstatus->pr_reg.fs = ctx->fs;
	prstatus->pr_reg.gs = ctx->gs;
	prstatus->pr_reg.pad = 0;
	prstatus->pr_reg.eip = ctx->eip;
	prstatus->pr_reg.cs = ctx->cs;
	prstatus->pr_reg.eflags = ctx->eflags;
	prstatus->pr_reg.esp = ctx->esp;
	prstatus->pr_reg.ss = ctx->ss;
	return 0;
}


void hal_createThreadAuxNotes(cpu_context_t *ctx, elf_thread_aux *note, const coredump_opts_t *opts)
{
	static const char FPREGSET_NAME[] = "CORE";
	if (opts->fpContext == 0) {
		return;
	}
	note->nhdr.n_namesz = sizeof(FPREGSET_NAME);
	note->nhdr.n_descsz = sizeof(ctx->fpuContext);
	note->nhdr.n_type = NT_FPREGSET;
	memcpy(note->name, FPREGSET_NAME, sizeof(FPREGSET_NAME));
	memcpy(&note->fpuContext, &ctx->fpuContext, sizeof(ctx->fpuContext));
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
	return (void *)ctx->esp;
}
