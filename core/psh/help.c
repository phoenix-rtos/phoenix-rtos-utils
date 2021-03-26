/*
 * Phoenix-RTOS
 *
 * Phoenix-RTOS SHell
 *
 * Copyright 2017, 2018, 2020, 2021 Phoenix Systems
 * Author: Pawel Pisarczyk, Jan Sikorski, Lukasz Kosinski, Mateusz Niewiadomski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <errno.h>
#include <stdio.h>


extern void psh_bindinfo(void);
extern void psh_catinfo(void);
extern void psh_execinfo(void);
extern void psh_killinfo(void);
extern void psh_lsinfo(void);
extern void psh_meminfo(void);
extern void psh_mkdirinfo(void);
extern void psh_mountinfo(void);
extern void psh_ncinfo(void);
extern void psh_perfinfo(void);
extern void psh_psinfo(void);
extern void psh_rebootinfo(void);
extern void psh_syncinfo(void);
extern void psh_sysexecinfo(void);
extern void psh_topinfo(void);
extern void psh_touchinfo(void);


void psh_helpinfo(void)
{
	printf("  help    - prints this help message\n");
}


void psh_help(void)
{
	printf("Available commands:\n");
	psh_bindinfo();
	psh_catinfo();
	psh_execinfo();
	printf("  exit    - exits the shell\n");
	psh_helpinfo();
	printf("  history - prints command history\n");
	psh_killinfo();
	psh_lsinfo();
	psh_meminfo();
	psh_mkdirinfo();
	psh_mountinfo();
	psh_ncinfo();
	psh_perfinfo();
	psh_psinfo();
	psh_rebootinfo();
	psh_syncinfo();
	psh_sysexecinfo();
	psh_topinfo();
	psh_touchinfo();
	return;
}
