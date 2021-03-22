/*
 * Phoenix-RTOS
 *
 * exec - replaces shell with the given command (empty implementation)
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


void psh_execinfo(void)
{
	return;
}


int psh_exec(int argc, char **argv)
{
	fprintf(stderr,"Command not supported!\n");
	return -ENOTSUP;
}
