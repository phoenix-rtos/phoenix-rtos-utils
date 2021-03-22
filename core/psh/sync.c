/*
 * Phoenix-RTOS
 *
 * sync - synchronizes device
 *
 * Copyright 2017, 2018, 2020, 2021 Phoenix Systems
 * Author: Pawel Pisarczyk, Jan Sikorski, Maciej Purski, Lukasz Kosinski, Mateusz Niewiadomski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <errno.h>
#include <stdio.h>

#include <sys/file.h>
#include <sys/mount.h>
#include <sys/msg.h>


void psh_syncinfo(void)
{
	printf("  sync    - synchronizes device\n");
}


int psh_sync(int argc, char **argv)
{
	msg_t msg = { 0 };
	oid_t oid;

	if (argc != 2) {
		fprintf(stderr, "usage: %s <device path>\n", argv[0]);
		return -EINVAL;
	}

	if (lookup(argv[1], NULL, &oid) < 0)
		return -ENXIO;

	msg.type = mtSync;

	return msgSend(oid.port, &msg);
}

