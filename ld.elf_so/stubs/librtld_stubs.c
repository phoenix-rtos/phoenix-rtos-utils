/*
 * Phoenix-RTOS
 *
 * librtld_stubs
 *
 * Copyright 2024 Phoenix Systems
 * Author: Hubert Badocha
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <stdlib.h>

#include "../include/NetBSD/dlfcn.h"
#include "../include/NetBSD/link_elf.h"


void *___dlauxinfo(void)
{
	abort();
}


int __dlctl(void *a, int b, void *c)
{
	abort();
}


int __dlinfo(void *a, int b, void *c)
{
	abort();
}


void *__dlvsym(void * restrict a, const char * restrict b, const char * restrict c)
{
	abort();
}


void ____dl_cxa_refcount(void *a, ssize_t b)
{
	abort();
}


int __dl_iterate_phdr(int (*a)(struct dl_phdr_info *, size_t, void *), void *b)
{
	abort();
}


extern void *_dlauxinfo(void) __attribute__((__pure__, weak, alias("___dlauxinfo")));

extern int	dlctl(void *, int, void *) __attribute__((weak, alias("__dlctl")));
extern int	dlinfo(void *, int, void *) __attribute__((weak, alias("__dlinfo")));
extern void *dlvsym(void * restrict, const char * restrict, const char * restrict) __attribute__((weak, alias("__dlvsym")));
extern void __dl_cxa_refcount(void *, ssize_t) __attribute__((weak, alias("____dl_cxa_refcount")));
extern int dl_iterate_phdr(int (*)(struct dl_phdr_info *, size_t, void *), void *) __attribute__((weak, alias("__dl_iterate_phdr")));
