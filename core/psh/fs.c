/*
 * Phoenix-RTOS
 *
 * fs - filesystem operations
 *
 * Copyright 2017, 2018, 2020 Phoenix Systems
 * Author: Pawel Pisarczyk, Jan Sikorski, Maciej Purski, Lukasz Kosinski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/file.h>
#include <sys/mount.h>
#include <sys/msg.h>
#include <sys/stat.h>
#include <sys/types.h>


int psh_mount(int argc, char **argv)
{
	int err;

	if (argc != 6) {
		printf("usage: %s <source> <target> <fstype> <mode> <data>\n", argv[0]);
		return -1;
	}

	if ((err = mount(argv[1], argv[2], argv[3], atoi(argv[4]), argv[5])) < 0)
		printf("mount: %s\n", strerror(err));

	return EOK;
}


int psh_bind(int argc, char **argv)
{
	msg_t msg = { 0 };
	oid_t soid, doid;
	struct stat buf;
	int err;

	if (argc != 3) {
		printf("usage: %s <source> <target>\n", argv[0]);
		return -1;
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

	if ((err = msgSend(doid.port, &msg)) < 0)
		return err;

	return msg.o.attr.val;
}


int psh_sync(int argc, char **argv)
{
	msg_t msg = { 0 };
	oid_t oid;

	if (argc != 2) {
		printf("usage: %s <device path>\n", argv[0]);
		return -1;
	}

	if (lookup(argv[1], NULL, &oid) < 0)
		return -1;

	msg.type = mtSync;

	return msgSend(oid.port, &msg);
}


int psh_touch(int argc, char **argv)
{
	FILE *file;
	int i;

	if (argc < 2) {
		printf("usage: %s <file path>...\n", argv[0]);
		return -1;
	}

	for (i = 1; i < argc; i++) {
		if ((file = fopen(argv[i], "w")) == NULL)
			printf("touch: failed to open %s\n", argv[i]);
		else
			fclose(file);
	}

	return EOK;
}


int psh_mkdir(int argc, char **argv)
{
	int i;

	if (argc < 2) {
		printf("usage: %s <dir path>...\n", argv[0]);
		return -1;
	}

	for (i = 1; i < argc; i++) {
		if (mkdir(argv[i], 0) < 0)
			printf("mkdir: failed to create %s directory\n", argv[i]);
	}

	return EOK;
}
