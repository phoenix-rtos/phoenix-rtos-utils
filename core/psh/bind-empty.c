/*
 * Phoenix-RTOS
 *
 * bind - binds device to directory (empty implementation)
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


void psh_bindinfo(void)
{
	return;
}


int psh_bind(int argc, char **argv)
{
	fprintf(stderr,"Command not supported!\n");
	return -ENOTSUP;
}
