/*
 * Phoenix-RTOS
 *
 * mem - view process or kernel memory map (empty implementation)
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


void psh_meminfo(void)
{
	return;
}


int psh_mem(int argc, char **argv)
{
	fprintf(stderr,"Command not supported!\n");
	return -ENOTSUP;
}
