/*
 * Phoenix-RTOS
 *
 * uptime - prints how long the system has been running
 *
 * Copyright 2021 Phoenix Systems
 * Author: Ziemowit Leszczynski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <errno.h>
#include <stdio.h>
#include <time.h>

#include "../psh.h"


void psh_uptimeinfo(void)
{
	printf("prints how long the system has been running");
}


int psh_uptime(int argc, char **argv)
{
	struct timespec tp;
	int rv = clock_gettime(CLOCK_MONOTONIC_RAW, &tp);
	if (rv < 0) {
		fprintf(stderr, "uptime: failed to get time\n");
		return -EINVAL;
	}
	printf("%jd\n", (intmax_t)tp.tv_sec);
	return EOK;
}


void __attribute__((constructor)) uptime_registerapp(void)
{
	static psh_appentry_t app = { .name = "uptime", .run = psh_uptime, .info = psh_uptimeinfo };
	psh_registerapp(&app);
}
