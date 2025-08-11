#include "cpu.h"
#include "hal.h"
#include "kernel_comm.h"

#include <string.h>

#define RET_THREAD_PSP 0xfffffffdu

int hal_fillPrStatus(cpu_context_t *ctx, elf_prstatus *prstatus, __u32 memRecvPort, const coredump_opts_t *opts)
{
	__u32 psp = ctx->psp;

	/* If we came from userspace HW ctx in on psp stack */
	if (ctx->irq_ret == RET_THREAD_PSP) {
		msg_t msg;
		msg_rid_t rid;
		msg.oid.port = memRecvPort;
		msg.oid.id = 0;

		int ret = coredump_getMemory((void *)ctx->psp, sizeof(struct cpu_hwContext_t), &msg, &rid);
		if (ret != 0) {
			PRINT_ERR("Failed to retrieve missing hw context with error %d\n", ret);
			return ret;
		}
		memcpy(&ctx->hwctx, msg.i.data, sizeof(struct cpu_hwContext_t));
		coredump_putMemory(&msg, rid);

		psp += sizeof(struct cpu_hwContext_t);
	}

	prstatus->pr_reg.r0 = ctx->hwctx.r0;
	prstatus->pr_reg.r1 = ctx->hwctx.r1;
	prstatus->pr_reg.r2 = ctx->hwctx.r2;
	prstatus->pr_reg.r3 = ctx->hwctx.r3;
	prstatus->pr_reg.r4 = ctx->r4;
	prstatus->pr_reg.r5 = ctx->r5;
	prstatus->pr_reg.r6 = ctx->r6;
	prstatus->pr_reg.r7 = ctx->r7;
	prstatus->pr_reg.r8 = ctx->r8;
	prstatus->pr_reg.r9 = ctx->r9;
	prstatus->pr_reg.r10 = ctx->r10;
	prstatus->pr_reg.fp = ctx->r11;
	prstatus->pr_reg.ip = ctx->hwctx.r12;
	prstatus->pr_reg.sp = psp;
	prstatus->pr_reg.lr = ctx->hwctx.lr;
	prstatus->pr_reg.pc = ctx->hwctx.pc;
	prstatus->pr_reg.psr = ctx->hwctx.psr;
	return 0;
}


void hal_createThreadAuxNotes(cpu_context_t *ctx, elf_thread_aux *note, const coredump_opts_t *opts)
{
	return;
}


void hal_createProcAuxNotes(elf_proc_aux *note, const coredump_opts_t *opts)
{
	return;
}


size_t hal_threadAuxNotesSize(const coredump_opts_t *opts)
{
	return 0;
}


size_t hal_procAuxNotesSize(const coredump_opts_t *opts)
{
	return 0;
}


void *hal_cpuGetUserSP(cpu_context_t *ctx)
{
	return (void *)ctx->psp;
}
