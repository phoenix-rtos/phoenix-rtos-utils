/*
 * Phoenix-RTOS
 *
 * ps - prints threads and processes (empty implementation)
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


void psh_psinfo(void)
{
	return;
}


int psh_ps(int argc, char **argv)
{
	fprintf(stderr,"Command not supported!\n");
	return -ENOTSUP;
}
