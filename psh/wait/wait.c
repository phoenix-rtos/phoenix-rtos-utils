/*
 * Phoenix-RTOS
 *
 * wait - waits for file to appear in the filesystem
 *
 * Copyright 2026 Phoenix Systems
 * Author: Jacek Maksymowicz
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <sys/msg.h>
#include <sys/minmax.h>
#include <unistd.h>

#include "../psh.h"


void psh_waitinfo(void)
{
	printf("waits for file to appear in the filesystem");
}


int psh_wait(int argc, char **argv)
{
	if ((argc != 2) && (argc != 3)) {
		fprintf(stderr, "usage: %s <target> [<timeout_ms>]\ndefault timeout: 1000 ms\n", argv[0]);
		return -EINVAL;
	}

	unsigned long long timeout_ms = 1000ULL;
	if (argc == 3) {
		char *end;
		timeout_ms = strtoull(argv[2], &end, 0);
		if (end == NULL || end == argv[2] || *end != '\0') {
			return -EINVAL;
		}
	}

	do {
		oid_t soid;
		int err = lookup(argv[1], NULL, &soid);
		if (err >= 0) {
			return 0;
		}
		else if (err != -ENOENT) {
			return err;
		}

		unsigned long long wait_ms = min(100ULL, timeout_ms);
		timeout_ms -= wait_ms;
		usleep(wait_ms * 1000LL);
	} while (timeout_ms != 0);

	return -ENOENT;
}


void __attribute__((constructor)) wait_registerapp(void)
{
	static psh_appentry_t app = { .name = "wait", .run = psh_wait, .info = psh_waitinfo };
	psh_registerapp(&app);
}
