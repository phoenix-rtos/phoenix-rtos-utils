/*
 * Phoenix-RTOS
 *
 * reboot - restart the machine (empty implementation)
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


void psh_rebootinfo(void)
{
	return;
}


int psh_reboot(int argc, char **argv)
{
	printf("Command not supported!");
	return -ENOTSUP;
}
