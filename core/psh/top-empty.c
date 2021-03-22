/*
 * Phoenix-RTOS
 *
 * top - prints threads and processes (empty implementation)
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


void psh_topinfo(void)
{
	return;
}


int psh_top(int argc, char **argv)
{
	fprintf(stderr,"Command not supported!\n");
	return -ENOTSUP;
}
