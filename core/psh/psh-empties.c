/*
 * Phoenix-RTOS
 *
 * Phoenix-RTOS SHell - empty, weak definitions of psh commands
 *
 * Copyright 2021 Phoenix Systems
 * Author: Mateusz Niewiadomski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <errno.h>
#include <stdio.h>

#include "psh.h"


void __attribute__((weak)) psh_bindinfo(void)
{
	return;
}


int __attribute__((weak)) psh_bind(int argc, char **argv)
{
	fprintf(stderr,psh_common.unkncmd);
	return -ENOTSUP;
}


void __attribute__((weak)) psh_catinfo(void)
{
	return;
}


int __attribute__((weak)) psh_cat(int argc, char **argv)
{
	fprintf(stderr,psh_common.unkncmd);
	return -ENOTSUP;
}


void __attribute__((weak)) psh_execinfo(void)
{
	return;
}


int __attribute__((weak)) psh_exec(int argc, char **argv)
{
	fprintf(stderr,psh_common.unkncmd);
	return -ENOTSUP;
}


void __attribute__((weak)) psh_helpinfo(void)
{
	return;
}


int __attribute__((weak)) psh_help(void)
{
	fprintf(stderr,psh_common.unkncmd);
	return -ENOTSUP;
}


void __attribute__((weak)) psh_killinfo(void)
{
	return;
}


int __attribute__((weak)) psh_kill(int argc, char **argv)
{
	fprintf(stderr,psh_common.unkncmd);
	return -ENOTSUP;
}


void __attribute__((weak)) psh_lsinfo(void)
{
	return;
}


int __attribute__((weak)) psh_ls(int argc, char **argv)
{
	fprintf(stderr,psh_common.unkncmd);
	return -ENOTSUP;
}


void __attribute__((weak)) psh_meminfo(void)
{
	return;
}


int __attribute__((weak)) psh_mem(int argc, char **argv)
{
	fprintf(stderr,psh_common.unkncmd);
	return -ENOTSUP;
}


void __attribute__((weak)) psh_mkdirinfo(void)
{
	return;
}


int __attribute__((weak)) psh_mkdir(int argc, char **argv)
{
	fprintf(stderr,psh_common.unkncmd);
	return -ENOTSUP;
}


void __attribute__((weak)) psh_mountinfo(void)
{
	return;
}


int __attribute__((weak)) psh_mount(int argc, char **argv)
{
	fprintf(stderr,psh_common.unkncmd);
	return -ENOTSUP;
}


void __attribute__((weak)) psh_ncinfo(void)
{
	return;
}


int __attribute__((weak)) psh_nc(int argc, char **argv)
{
	fprintf(stderr,psh_common.unkncmd);
	return -ENOTSUP;
}


void __attribute__((weak)) psh_perfinfo(void)
{
	return;
}


int __attribute__((weak)) psh_perf(int argc, char **argv)
{
	fprintf(stderr,psh_common.unkncmd);
	return -ENOTSUP;
}


void __attribute__((weak)) psh_psinfo(void)
{
	return;
}


int __attribute__((weak)) psh_ps(int argc, char **argv)
{
	fprintf(stderr,psh_common.unkncmd);
	return -ENOTSUP;
}



void __attribute__((weak)) psh_rebootinfo(void)
{
	return;
}


int __attribute__((weak)) psh_reboot(int argc, char **argv)
{
	printf("Command not supported!");
	return -ENOTSUP;
}


int __attribute__((weak)) psh_runfile(int argc, char **argv)
{
	fprintf(stderr,psh_common.unkncmd);
	return -ENOTSUP;
}


void __attribute__((weak)) psh_syncinfo(void)
{
	return;
}


int __attribute__((weak)) psh_sync(int argc, char **argv)
{
	fprintf(stderr,psh_common.unkncmd);
	return -ENOTSUP;
}


void __attribute__((weak)) psh_sysexecinfo(void)
{
	return;
}


int __attribute__((weak)) psh_sysexec(int argc, char **argv)
{
	fprintf(stderr,psh_common.unkncmd);
	return -ENOTSUP;
}


void __attribute__((weak)) psh_topinfo(void)
{
	return;
}


int __attribute__((weak)) psh_top(int argc, char **argv)
{
	fprintf(stderr,psh_common.unkncmd);
	return -ENOTSUP;
}


void __attribute__((weak)) psh_touchinfo(void)
{
	return;
}


int __attribute__((weak)) psh_touch(int argc, char **argv)
{
	fprintf(stderr,psh_common.unkncmd);
	return -ENOTSUP;
}
