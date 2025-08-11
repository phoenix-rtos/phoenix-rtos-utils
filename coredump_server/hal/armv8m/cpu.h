#ifndef _CPU_H_
#define _CPU_H_

#include "elf.h"
#include <phoenix/types.h>

#define HAL_ELF_MACHINE 0x28

struct pr_regs {
	__u32 r0;
	__u32 r1;
	__u32 r2;
	__u32 r3;
	__u32 r4;
	__u32 r5;
	__u32 r6;
	__u32 r7;
	__u32 r8;
	__u32 r9;
	__u32 r10;
	__u32 fp;
	__u32 ip;
	__u32 sp;
	__u32 lr;
	__u32 pc;
	__u32 psr;

	__u32 pad;
};


struct elf_prstatus {
	struct elf_siginfo {
		int si_signo;
		int si_code;
		int si_errno;
	} pr_info;
	short pr_cursig;
	unsigned long pr_sigpend;
	unsigned long pr_sighold;
	pid_t pr_pid;
	pid_t pr_ppid;
	pid_t pr_pgrp;
	pid_t pr_sid;
	struct timeval {
		long tv_sec;
		long tv_usec;
	} pr_utime;
	struct timeval pr_stime;
	struct timeval pr_cutime;
	struct timeval pr_cstime;
	struct pr_regs pr_reg;
	int pr_fpvalid;
};


struct elf_thread_aux {
	Elf32_Nhdr nhdr;
	char name[8];

	struct {
		__u32 freg[16 * 2];
		__u32 pad[16 * 2];
		__u32 fpsr;
	} fpuContext;
};


struct elf_proc_aux {
	Elf32_Nhdr nhdr;
	char name[8];
	struct {
		__u32 a_type;
		__u32 a_val;
	} auxv[2];
};


/* CPU context saved by interrupt handlers on thread kernel stack */
struct cpu_context_t {
	__u32 savesp;
	__u32 fpuctx;

	/* Saved by ISR */
	__u32 psp;
	__u32 r4;
	__u32 r5;
	__u32 r6;
	__u32 r7;
	__u32 r8;
	__u32 r9;
	__u32 r10;
	__u32 r11;
	__u32 irq_ret;

	__u32 msp;
	__u32 pad0;

	struct cpu_hwContext_t {
		__u32 r0;
		__u32 r1;
		__u32 r2;
		__u32 r3;
		__u32 r12;
		__u32 lr;
		__u32 pc;
		__u32 psr;
	} hwctx;
};

#endif /* _CPU_H_ */
