#ifndef _CPU_H_
#define _CPU_H_

#include "elf.h"
#include <phoenix/types.h>

#define HAL_ELF_MACHINE 0xF3

struct pr_regs {
	__u64 sepc;
	__u64 ra;
	__u64 sp;
	__u64 gp;
	__u64 tp;
	__u64 t0;
	__u64 t1;
	__u64 t2;
	__u64 s0;
	__u64 s1;
	__u64 a0;
	__u64 a1;
	__u64 a2;
	__u64 a3;
	__u64 a4;
	__u64 a5;
	__u64 a6;
	__u64 a7;
	__u64 s2;
	__u64 s3;
	__u64 s4;
	__u64 s5;
	__u64 s6;
	__u64 s7;
	__u64 s8;
	__u64 s9;
	__u64 s10;
	__u64 s11;
	__u64 t3;
	__u64 t4;
	__u64 t5;
	__u64 t6;
} __attribute__((packed, aligned(8)));


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
	__u64 ft0;
	__u64 ft1;
	__u64 ft2;
	__u64 ft3;
	__u64 ft4;
	__u64 ft5;
	__u64 ft6;
	__u64 ft7;

	__u64 fs0;
	__u64 fs1;

	__u64 fa0;
	__u64 fa1;
	__u64 fa2;
	__u64 fa3;
	__u64 fa4;
	__u64 fa5;
	__u64 fa6;
	__u64 fa7;

	__u64 fs2;
	__u64 fs3;
	__u64 fs4;
	__u64 fs5;
	__u64 fs6;
	__u64 fs7;
	__u64 fs8;
	__u64 fs9;
	__u64 fs10;
	__u64 fs11;

	__u64 ft8;
	__u64 ft9;
	__u64 ft10;
	__u64 ft11;

	__u64 fcsr;
} __attribute__((packed)) cpu_fpContext_t;


struct elf_thread_aux {
	Elf32_Nhdr nhdr;
	char name[8];
	cpu_fpContext_t fpCtx;
} __attribute__((packed));


struct elf_proc_aux {
};


struct cpu_context_t {
	__u64 ra; /* x1 */
	__u64 gp; /* x3 */

	__u64 t0; /* x5 */
	__u64 t1; /* x6 */
	__u64 t2; /* x7 */

	__u64 s0; /* x8 */
	__u64 s1; /* x9 */
	__u64 a0; /* x10 */
	__u64 a1; /* x11 */

	__u64 a2; /* x12 */
	__u64 a3; /* x13 */
	__u64 a4; /* x14 */
	__u64 a5; /* x15 */

	__u64 a6; /* x16 */
	__u64 a7; /* x17 */
	__u64 s2; /* x18 */
	__u64 s3; /* x19 */

	__u64 s4; /* x20 */
	__u64 s5; /* x21 */
	__u64 s6; /* x22 */
	__u64 s7; /* x23 */

	__u64 s8;  /* x24 */
	__u64 s9;  /* x25 */
	__u64 s10; /* x26 */
	__u64 s11; /* x27 */

	__u64 t3; /* x28 */
	__u64 t4; /* x29 */
	__u64 t5; /* x30 */
	__u64 t6; /* x31 */

	__u64 ksp;
	__u64 sstatus;
	__u64 sepc;
	__u64 stval;
	__u64 scause;
	__u64 sscratch;

	__u64 tp;
	__u64 sp;

	cpu_fpContext_t fpCtx;
} __attribute__((packed, aligned(8)));

#endif /* _CPU_H_ */
