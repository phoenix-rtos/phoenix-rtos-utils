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
	int c, fd;
	ssize_t n;

	for (;;) {
		c = getopt(argc, argv, "h");
		if (c == -1) {
			break;
		}
		switch (c) {
			case 'h':
			default:
				psh_dmesg_help(argv[0]);
				return EXIT_FAILURE;
		}
	}

	fd = open(_PATH_KLOG, O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		fprintf(stderr, "dmesg: Fail to open %s\n", _PATH_KLOG);
		return 1;
	}

	for (;;) {
		n = read(fd, buf, sizeof(buf));
		if (n <= 0) {
			if ((n == 0) || (errno == EINTR) || (errno == EPIPE)) {
				continue;
			}
			else {
				break;
			}
		}
		if (psh_write(STDOUT_FILENO, buf, n) != n) {
			close(fd);
			return EXIT_FAILURE;
		}
	}
	close(fd);

	return EXIT_SUCCESS;
}


void __attribute__((constructor)) dmesg_registerapp(void)
{
	static psh_appentry_t app = { .name = "dmesg", .run = psh_dmesg, .info = psh_dmesginfo };
	psh_registerapp(&app);
}
