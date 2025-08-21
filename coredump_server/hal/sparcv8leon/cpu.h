#ifndef _CPU_H_
#define _CPU_H_

#include "elf.h"
#include <unistd.h>
#include <phoenix/types.h>

#define HAL_ELF_MACHINE 0x02

struct prgregset_t {
	__u32 pad;
	__u32 g1;
	__u32 g2;
	__u32 g3;
	__u32 g4;
	__u32 g5;
	__u32 g6;
	__u32 g7;
	__u32 o0;
	__u32 o1;
	__u32 o2;
	__u32 o3;
	__u32 o4;
	__u32 o5;
	__u32 sp;
	__u32 o7;

	__u32 l0;
	__u32 l1;
	__u32 l2;
	__u32 l3;
	__u32 l4;
	__u32 l5;
	__u32 l6;
	__u32 l7;
	__u32 i0;
	__u32 i1;
	__u32 i2;
	__u32 i3;
	__u32 i4;
	__u32 i5;
	__u32 fp;
	__u32 i7;

	__u32 psr;
	__u32 pc;
	__u32 npc;
	__u32 y;
	__u32 pad1;
	__u32 pad2;
};


/* prstatus_t from opensolaris modified to match GDB: removed pr_sysarg, swapped pr_who and pr_pid */
struct elf_prstatus {
	int pr_flags;  /* Flags (see below) */
	short pr_why;  /* Reason for process stop (if stopped) */
	short pr_what; /* More detailed reason */
	struct siginfo {
		int si_signo; /* signal from signal.h */
		int si_code;  /* code from above      */
		int si_errno; /* error from errno.h   */
		union {
			int _pad[((128 / sizeof(int)) - 3)]; /* for future growth    */
			struct {                             /* kill(), SIGCLD       */
				long _pid;                       /* process ID           */
				union {
					struct {
						long _uid;
					} _kill;
					struct {
						long _utime;
						int _status;
						long _stime;
					} _cld;
				} _pdata;
			} _proc;
			struct {         /* SIGSEGV, SIGBUS, SIGILL and SIGFPE   */
				char *_addr; /* faulting address     */
			} _fault;
			struct {     /* SIGPOLL, SIGXFSZ     */
						 /* fd not currently available for SIGPOLL */
				int _fd; /* file descriptor      */
				long _band;
			} _file;

		} _data;
	} pr_info;                     /* Info associated with signal or fault */
	short pr_cursig;               /* Current signal */
	__u16 pr_nlwp;                 /* Number of lwps in the process */
	struct sigset {                /* signal set type */
		unsigned int __sigbits[4]; /* signal bits */
	} pr_sigpend;                  /* Set of signals pending to the process */
	struct sigset pr_sighold;      /* Set of signals held (blocked) by the lwp */
	struct sigaltstack {
		void *ss_sp;
		size_t ss_size;
		int ss_flags;
	} pr_altstack; /* Alternate signal stack info */
	struct sol_sigaction {
		int sa_flags;
		void (*sa_handler)(int);
		struct sigset sa_mask;
		int sa_resv[2];
	} pr_action;                /* Signal action for current signal */
	id_t pr_who;                /* (originally pr_pid) Specific lwp identifier */
	pid_t pr_ppid;              /* Parent process id */
	pid_t pr_pgrp;              /* Process group id */
	pid_t pr_sid;               /* Session id */
	struct timestruc {          /* definition per POSIX.4 */
		time_t tv_sec;          /* seconds */
		long tv_nsec;           /* and nanoseconds */
	} pr_utime;                 /* Process user cpu time */
	struct timestruc pr_stime;  /* Process system cpu time */
	struct timestruc pr_cutime; /* Sum of children's user times */
	struct timestruc pr_cstime; /* Sum of children's system times */
	char pr_clname[8];          /* Scheduling class name */
	short pr_syscall;           /* System call number (if in syscall) */
	short pr_nsysarg;           /* Number of arguments to this syscall */
	// long pr_sysarg[8];              /* Arguments to this syscall */
	pid_t pr_pid;                   /* (originally pr_who) Process id */
	struct sigset pr_lwppend;       /* Set of signals pending to the lwp */
	struct ucontext *pr_oldcontext; /* Address of previous ucontext */
	caddr_t pr_brkbase;             /* Address of the process heap */
	size_t pr_brksize;              /* Size of the process heap, in bytes */
	caddr_t pr_stkbase;             /* Address of the process stack */
	size_t pr_stksize;              /* Size of the process stack, in bytes */
	short pr_processor;             /* processor which last ran this LWP */
	short pr_bind;                  /* processor LWP bound to or PBIND_NONE */
	long pr_instr;                  /* Current instruction */
	struct prgregset_t pr_reg;      /* General registers */
} __attribute__((packed));


struct elf_thread_aux {
	Elf32_Nhdr nhdr;
	char name[8];

	__u32 f0;
	__u32 f1;
	__u32 f2;
	__u32 f3;
	__u32 f4;
	__u32 f5;
	__u32 f6;
	__u32 f7;
	__u32 f8;
	__u32 f9;
	__u32 f10;
	__u32 f11;
	__u32 f12;
	__u32 f13;
	__u32 f14;
	__u32 f15;
	__u32 f16;
	__u32 f17;
	__u32 f18;
	__u32 f19;
	__u32 f20;
	__u32 f21;
	__u32 f22;
	__u32 f23;
	__u32 f24;
	__u32 f25;
	__u32 f26;
	__u32 f27;
	__u32 f28;
	__u32 f29;
	__u32 f30;
	__u32 f31;

	__u32 pad;
	__u32 fsr;

	__u32 ctrl;
	__u32 pad1[64];
} __attribute__((packed));


struct elf_proc_aux {
};


typedef struct {
	__u32 f0;
	__u32 f1;
	__u32 f2;
	__u32 f3;
	__u32 f4;
	__u32 f5;
	__u32 f6;
	__u32 f7;
	__u32 f8;
	__u32 f9;
	__u32 f10;
	__u32 f11;
	__u32 f12;
	__u32 f13;
	__u32 f14;
	__u32 f15;
	__u32 f16;
	__u32 f17;
	__u32 f18;
	__u32 f19;
	__u32 f20;
	__u32 f21;
	__u32 f22;
	__u32 f23;
	__u32 f24;
	__u32 f25;
	__u32 f26;
	__u32 f27;
	__u32 f28;
	__u32 f29;
	__u32 f30;
	__u32 f31;

	__u32 fsr;
	__u32 pad;
} cpu_fpContext_t;


typedef struct {
	/* local */
	__u32 l0;
	__u32 l1;
	__u32 l2;
	__u32 l3;
	__u32 l4;
	__u32 l5;
	__u32 l6;
	__u32 l7;

	/* in */
	__u32 i0;
	__u32 i1;
	__u32 i2;
	__u32 i3;
	__u32 i4;
	__u32 i5;
	__u32 fp;
	__u32 i7;
} __attribute__((packed)) cpu_winContext_t;


struct cpu_context_t {
	__u32 savesp;

	__u32 y;
	__u32 psr;
	__u32 pc;
	__u32 npc;

	/* global */
	__u32 g1;
	__u32 g2;
	__u32 g3;
	__u32 g4;
	__u32 g5;
	__u32 g6;
	__u32 g7;

	/* out */
	__u32 o0;
	__u32 o1;
	__u32 o2;
	__u32 o3;
	__u32 o4;
	__u32 o5;
	__u32 sp;
	__u32 o7;

	cpu_fpContext_t fpCtx;
	cpu_winContext_t winCtx;
} __attribute__((packed));

#endif /* _CPU_H_ */
