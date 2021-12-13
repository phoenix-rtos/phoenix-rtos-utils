/*
 * Phoenix-RTOS
 *
 * dmesg - reads kernel ring buffer
 *
 * Copyright 2021 Phoenix Systems
 * Author: Maciej Purski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../psh.h"


void psh_dmesginfo(void)
{
	printf("read kernel ring buffer");
}


static void psh_dmesg_help(const char *prog)
{
	printf("Usage: %s [options] [files]\n", prog);
	printf("  -h:  shows this help message\n");
}


int psh_dmesg(int argc, char **argv)
{
	char buf[256];
	int n, written, ret;
	int c, fd;

	while ((c = getopt(argc, argv, "h")) != -1) {
		switch (c) {
			case 'h':
			default:
				psh_dmesg_help(argv[0]);
				return EOK;
		}
	}

	if ((fd = open(_PATH_KLOG, O_RDONLY | O_NONBLOCK)) < 0) {
		printf("dmesg: Fail to open %s\n", _PATH_KLOG);
		return 1;
	}

	while (1) {
		n = read(fd, buf, sizeof(buf));
		if (n < 0) {
			if (errno == -EINTR || errno == -EPIPE)
				continue;
			else
				break;
		}
		written = 0;
		while (written < n) {
			ret = write(STDOUT_FILENO, buf + written, n - written);
			if (ret > 0) {
				written += ret;
			}
			else if (errno == EINTR) {
				continue;
			}
			else {
				close(fd);
				return 1;
			}
		}
	}
	close(fd);

	return 0;
}


void __attribute__((constructor)) dmesg_registerapp(void)
{
	static psh_appentry_t app = { .name = "dmesg", .run = psh_dmesg, .info = psh_dmesginfo };
	psh_registerapp(&app);
}
