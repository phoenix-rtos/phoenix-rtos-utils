#ifndef _CPU_H_
#define _CPU_H_

#include "elf.h"
#include <phoenix/types.h>

#define HAL_ELF_MACHINE 0x03

struct pr_regs {
	__u32 ebx;
	__u32 ecx;
	__u32 edx;
	__u32 esi;
	__u32 edi;
	__u32 ebp;
	__u32 eax;
	__u32 ds;
	__u32 es;
	__u32 fs;
	__u32 gs;
	__u32 pad;
	__u32 eip;
	__u32 cs;
	__u32 eflags;
	__u32 esp;
	__u32 ss;
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


typedef struct {
	__u16 controlWord, _controlWord;
	__u16 statusWord, _statusWord;
	__u16 tagWord, _tagWord;
	__u32 fip;
	__u32 fips;
	__u32 fdp;
	__u16 fds, _fds;
	__u8 fpuContext[80];
} fpu_context_t;


struct elf_thread_aux {
	Elf32_Nhdr nhdr;
	char name[8];
	fpu_context_t fpuContext;
};


struct elf_proc_aux {
};


struct cpu_context_t {
	__u32 savesp;
	__u32 edi;
	__u32 esi;
	__u32 ebp;
	__u32 edx;
	__u32 ecx;
	__u32 ebx;
	__u32 eax;
	__u16 gs;
	__u16 fs;
	__u16 es;
	__u16 ds;
	fpu_context_t fpuContext;
	__u32 cr0Bits;
	__u32 eip; /* eip, cs, eflags, esp, ss saved by CPU on interrupt */
	__u32 cs;
	__u32 eflags;
	__u32 esp;
	__u32 ss;
};

#endif /* _CPU_H_ */
