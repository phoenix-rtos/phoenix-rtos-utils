#include "cpu.h"
#include "hal.h"
#include "kernel_comm.h"
#include <string.h>

int hal_fillPrStatus(cpu_context_t *ctx, elf_prstatus *prstatus, __u32 memRecvPort, const coredump_opts_t *opts)
{
	prstatus->pr_reg.g1 = ctx->g1;
	prstatus->pr_reg.g2 = ctx->g2;
	prstatus->pr_reg.g3 = ctx->g3;
	prstatus->pr_reg.g4 = ctx->g4;
	prstatus->pr_reg.g5 = ctx->g5;
	prstatus->pr_reg.g6 = ctx->g6;
	prstatus->pr_reg.g7 = ctx->g7;
	prstatus->pr_reg.o0 = ctx->o0;
	prstatus->pr_reg.o1 = ctx->o1;
	prstatus->pr_reg.o2 = ctx->o2;
	prstatus->pr_reg.o3 = ctx->o3;
	prstatus->pr_reg.o4 = ctx->o4;
	prstatus->pr_reg.o5 = ctx->o5;
	prstatus->pr_reg.sp = ctx->sp;
	prstatus->pr_reg.o7 = ctx->o7;

	prstatus->pr_reg.psr = ctx->psr;
	prstatus->pr_reg.pc = ctx->pc;
	prstatus->pr_reg.npc = ctx->npc;
	prstatus->pr_reg.y = ctx->y;

	/* Fetch cpu_winContext_t from user stack */
	msg_t msg;
	msg_rid_t rid;
	msg.oid.port = memRecvPort;
	msg.oid.id = 0;

	int ret = coredump_getMemory((void *)ctx->sp, sizeof(cpu_winContext_t), &msg, &rid);
	if (ret != 0) {
		PRINT_ERR("Failed to retrieve missing win context with error %d\n", ret);
		return ret;
	}
	memcpy(&ctx->winCtx, msg.i.data, sizeof(cpu_winContext_t));
	coredump_putMemory(&msg, rid);

	prstatus->pr_reg.l0 = ctx->winCtx.l0;
	prstatus->pr_reg.l1 = ctx->winCtx.l1;
	prstatus->pr_reg.l2 = ctx->winCtx.l2;
	prstatus->pr_reg.l3 = ctx->winCtx.l3;
	prstatus->pr_reg.l4 = ctx->winCtx.l4;
	prstatus->pr_reg.l5 = ctx->winCtx.l5;
	prstatus->pr_reg.l6 = ctx->winCtx.l6;
	prstatus->pr_reg.l7 = ctx->winCtx.l7;
	prstatus->pr_reg.i0 = ctx->winCtx.i0;
	prstatus->pr_reg.i1 = ctx->winCtx.i1;
	prstatus->pr_reg.i2 = ctx->winCtx.i2;
	prstatus->pr_reg.i3 = ctx->winCtx.i3;
	prstatus->pr_reg.i4 = ctx->winCtx.i4;
	prstatus->pr_reg.i5 = ctx->winCtx.i5;
	prstatus->pr_reg.fp = ctx->winCtx.fp;
	prstatus->pr_reg.i7 = ctx->winCtx.i7;

	return 0;
}


void hal_createThreadAuxNotes(cpu_context_t *ctx, elf_thread_aux *note, const coredump_opts_t *opts)
{
	static const char FPREGSET_NAME[] = "CORE";
	if (opts->fpContext == 0) {
		return;
	}

	note->nhdr.n_namesz = sizeof(FPREGSET_NAME);
	note->nhdr.n_descsz = sizeof(elf_thread_aux) - offsetof(elf_thread_aux, f0);
	note->nhdr.n_type = NT_FPREGSET;
	memcpy(&note->name, FPREGSET_NAME, sizeof(FPREGSET_NAME));

	note->f0 = ctx->fpCtx.f0;
	note->f1 = ctx->fpCtx.f1;
	note->f2 = ctx->fpCtx.f2;
	note->f3 = ctx->fpCtx.f3;
	note->f4 = ctx->fpCtx.f4;
	note->f5 = ctx->fpCtx.f5;
	note->f6 = ctx->fpCtx.f6;
	note->f7 = ctx->fpCtx.f7;
	note->f8 = ctx->fpCtx.f8;
	note->f9 = ctx->fpCtx.f9;
	note->f10 = ctx->fpCtx.f10;
	note->f11 = ctx->fpCtx.f11;
	note->f12 = ctx->fpCtx.f12;
	note->f13 = ctx->fpCtx.f13;
	note->f14 = ctx->fpCtx.f14;
	note->f15 = ctx->fpCtx.f15;
	note->f16 = ctx->fpCtx.f16;
	note->f17 = ctx->fpCtx.f17;
	note->f18 = ctx->fpCtx.f18;
	note->f19 = ctx->fpCtx.f19;
	note->f20 = ctx->fpCtx.f20;
	note->f21 = ctx->fpCtx.f21;
	note->f22 = ctx->fpCtx.f22;
	note->f23 = ctx->fpCtx.f23;
	note->f24 = ctx->fpCtx.f24;
	note->f25 = ctx->fpCtx.f25;
	note->f26 = ctx->fpCtx.f26;
	note->f27 = ctx->fpCtx.f27;
	note->f28 = ctx->fpCtx.f28;
	note->f29 = ctx->fpCtx.f29;
	note->f30 = ctx->fpCtx.f30;
	note->f31 = ctx->fpCtx.f31;
	note->pad = 0;
	note->fsr = ctx->fpCtx.fsr;
	note->ctrl = (__u32)((1 << 8) | (8 << 16));
	memset(note->pad1, 0, sizeof(note->pad1));
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
