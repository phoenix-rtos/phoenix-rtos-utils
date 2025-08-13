#include "cpu.h"
#include "hal.h"
#include "kernel_comm.h"

#include <string.h>

#ifdef CPU_IMXRT
#define RET_THREAD_PSP 0xffffffed
#define SIZE_FPUCTX    0x48
#else
#define RET_THREAD_PSP 0xfffffffd
#endif

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
	}
	psp += sizeof(struct cpu_hwContext_t);
#ifdef CPU_IMXRT /* FIXME - check if FPU was enabled instead */
	psp += SIZE_FPUCTX;
#endif

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
#ifndef CPU_IMXRT
	return;
#else
	static const char ARMVFP_NAME[] = "LINUX";
	if (opts->fpContext == 0) {
		return;
	}
	note->nhdr.n_namesz = sizeof(ARMVFP_NAME);
	note->nhdr.n_descsz = sizeof(note->fpuContext);
	note->nhdr.n_type = NT_ARM_VFP;
	memcpy(note->name, ARMVFP_NAME, sizeof(ARMVFP_NAME));

	note->fpuContext.freg[0] = ctx->s0;
	note->fpuContext.freg[1] = ctx->s1;
	note->fpuContext.freg[2] = ctx->s2;
	note->fpuContext.freg[3] = ctx->s3;
	note->fpuContext.freg[4] = ctx->s4;
	note->fpuContext.freg[5] = ctx->s5;
	note->fpuContext.freg[6] = ctx->s6;
	note->fpuContext.freg[7] = ctx->s7;
	note->fpuContext.freg[8] = ctx->s8;
	note->fpuContext.freg[9] = ctx->s9;
	note->fpuContext.freg[10] = ctx->s10;
	note->fpuContext.freg[11] = ctx->s11;
	note->fpuContext.freg[12] = ctx->s12;
	note->fpuContext.freg[13] = ctx->s13;
	note->fpuContext.freg[14] = ctx->s14;
	note->fpuContext.freg[15] = ctx->s15;
	note->fpuContext.freg[16] = ctx->s16;
	note->fpuContext.freg[17] = ctx->s17;
	note->fpuContext.freg[18] = ctx->s18;
	note->fpuContext.freg[19] = ctx->s19;
	note->fpuContext.freg[20] = ctx->s20;
	note->fpuContext.freg[21] = ctx->s21;
	note->fpuContext.freg[22] = ctx->s22;
	note->fpuContext.freg[23] = ctx->s23;
	note->fpuContext.freg[24] = ctx->s24;
	note->fpuContext.freg[25] = ctx->s25;
	note->fpuContext.freg[26] = ctx->s26;
	note->fpuContext.freg[27] = ctx->s27;
	note->fpuContext.freg[28] = ctx->s28;
	note->fpuContext.freg[29] = ctx->s29;
	note->fpuContext.freg[30] = ctx->s30;
	note->fpuContext.freg[31] = ctx->s31;
	note->fpuContext.fpsr = ctx->fpscr;
#endif /* CPU_IMXRT */
}


void hal_createProcAuxNotes(elf_proc_aux *note, const coredump_opts_t *opts)
{
#ifndef CPU_IMXRT
	return;
#endif
	if (opts->fpContext == 0) {
		return;
	}
	static const char AUXV_NAME[] = "CORE";

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
#ifdef CPU_IMXRT
	if (opts->fpContext) {
		return sizeof(elf_thread_aux);
	}
#endif
	return 0;
}


size_t hal_procAuxNotesSize(const coredump_opts_t *opts)
{
#ifdef CPU_IMXRT
	if (opts->fpContext) {
		return sizeof(elf_proc_aux);
	}
#endif
	return 0;
}


void *hal_cpuGetUserSP(cpu_context_t *ctx)
{

	__u32 psp = ctx->psp;
	psp += sizeof(struct cpu_hwContext_t);
#ifdef CPU_IMXRT /* FIXME - check if FPU was enabled instead */
	psp += SIZE_FPUCTX;
#endif
	return (void *)psp;
}
