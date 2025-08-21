#ifndef _CPU_H_
#define _CPU_H_

#include "elf.h"
#include <phoenix/types.h>

#define HAL_ELF_MACHINE 0xB7

struct pr_regs {
	__u64 x[31]; /* General purpose registers */
	__u64 sp;
	__u64 pc;
	__u64 psr;
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
		__u64 freg[2 * 32];
		__u32 fpsr;
		__u32 fpcr;
		__u64 pad;
	} fpuContext;
} __attribute__((packed));


struct elf_proc_aux {
};


/* CPU context saved by interrupt handlers on thread kernel stack */
struct cpu_context_t {
	__u64 savesp;
	__u64 cpacr;
#ifndef __SOFTFP__
	/* Advanced SIMD/FPU */
	__u64 fpcr;
	__u64 fpsr;
	__u64 freg[2 * 32];
#endif

	__u64 psr;
	__u64 pc;
	__u64 x[31]; /* General purpose registers */
	__u64 sp;
};

#endif /* _CPU_H_ */
