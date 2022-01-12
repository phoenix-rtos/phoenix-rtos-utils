/*
 * Phoenix-RTOS
 *
 * bind - binds device to directory
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
#include <sys/msg.h>
#include <sys/stat.h>

#include "../psh.h"


void psh_bindinfo(void)
{
	printf("binds device to directory");
}


int psh_bind(int argc, char **argv)
{
	msg_t msg = { 0 };
	oid_t soid, doid;
	struct stat buf;
	int err;

	if (argc != 3) {
		fprintf(stderr, "usage: %s <source> <target>\n", argv[0]);
		return -EINVAL;
	}

	if (lookup(argv[1], NULL, &soid) < 0)
		return -ENOENT;

	if (lookup(argv[2], NULL, &doid) < 0)
		return -ENOENT;

	if ((err = stat(argv[2], &buf)))
		return err;

	if (!S_ISDIR(buf.st_mode))
		return -ENOTDIR;

	msg.type = mtSetAttr;
	msg.i.attr.oid = doid;
	msg.i.attr.type = atDev;
	msg.i.data = &soid;
	msg.i.size = sizeof(oid_t);

	if (((err = msgSend(doid.port, &msg)) < 0) || ((err = msg.o.attr.err) < 0))
		return err;

	return EOK;
}


void __attribute__((constructor)) bind_registerapp(void)
{
	static psh_appentry_t app = {.name = "bind", .run = psh_bind, .info = psh_bindinfo};
	psh_registerapp(&app);
}
