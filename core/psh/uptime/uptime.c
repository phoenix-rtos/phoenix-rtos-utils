/*
 * Phoenix-RTOS
 *
 * uptime - prints how long the system has been running
 *
 * Copyright 2021 Phoenix Systems
 * Author: Ziemowit Leszczynski, Gerard Swiderski
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
	intmax_t days, seconds;
	int minutes, hours, rv;

	rv = clock_gettime(CLOCK_MONOTONIC_RAW, &tp);
	if (rv < 0) {
		fprintf(stderr, "uptime: failed to get time\n");
		return -EINVAL;
	}

	seconds = tp.tv_sec;

	if (argc == 1 && seconds >= 0) {
		days = seconds / 86400;
		seconds %= 86400;
		hours = seconds / 3600;
		seconds %= 3600;
		minutes = seconds / 60;
		seconds %= 60;

		printf("up ");

		if (days > 0)
			printf("%jd day%s and ", days, days == 1 ? "" : "s");

		printf("%02d:%02d:%02d\n", hours, minutes, (int)seconds);
	}
	else {
		printf("%jd\n", seconds);
	}


	return EOK;
}


void __attribute__((constructor)) uptime_registerapp(void)
{
	static psh_appentry_t app = { .name = "uptime", .run = psh_uptime, .info = psh_uptimeinfo };
	psh_registerapp(&app);
}
