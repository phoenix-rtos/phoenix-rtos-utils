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

	err = lookup(argv[1], NULL, &soid);
	if (err < 0) {
		fprintf(stderr, "bind: lookup(source) = %d\n", err);
		return -ENOENT;
	}

	err = lookup(argv[2], NULL, &doid);
	if (err < 0) {
		fprintf(stderr, "bind: lookup(target) = %d\n", err);
		return -ENOENT;
	}

	err = stat(argv[2], &buf);
	if (err != 0) {
		fprintf(stderr, "bind: stat failed with %d\n", err);
		return err;
	}

	if (!S_ISDIR(buf.st_mode)) {
		fprintf(stderr, "bind: target is not a directory\n");
		return -ENOTDIR;
	}

	msg.type = mtSetAttr;
	msg.oid = doid;
	msg.i.attr.type = atDev;
	msg.i.data = &soid;
	msg.i.size = sizeof(oid_t);

	err = msgSend(doid.port, &msg);

	if (err != 0) {
		fprintf(stderr, "bind: msgSend failed with %d\n", err);
		return err;
	}
	else if (msg.o.err != 0) {
		fprintf(stderr, "bind: server responded with %d\n", msg.o.err);
		return msg.o.err;
	}

	return EOK;
}


void __attribute__((constructor)) bind_registerapp(void)
{
	static psh_appentry_t app = {.name = "bind", .run = psh_bind, .info = psh_bindinfo};
	psh_registerapp(&app);
}
