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
#include <stdbool.h>
#include <string.h>

#include "../psh.h"


void psh_dmesginfo(void)
{
	printf("read kernel ring buffer");
}


static void psh_dmesg_help(const char *prog)
{
	printf("Usage: %s [options]\n", prog);
	printf("  -D:  disable the printing of messages to the console\n");
	printf("  -E:  enable the printing of messages to the console\n");
	printf("  -h:  shows this help message\n");
}


static int psh_kmsgctrl(bool state)
{
	int fd = open("devfs/kmsgctrl", O_WRONLY);
	if (fd < 0) {
		fd = open("/dev/kmsgctrl", O_WRONLY);
	}

	if (fd < 0) {
		perror("dmesg: Failed to open kmsgctrl");
		return EXIT_FAILURE;
	}

	size_t err = psh_write(fd, state ? "1" : "0", 2);

	close(fd);

	if (err != 2) {
		perror("dmesg: Write to kmsgctrl failed");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}


int psh_dmesg(int argc, char **argv)
{
	bool help = false, logDis = false, logEn = false;

	for (;;) {
		int c = getopt(argc, argv, "hDE");
		if (c == -1) {
			break;
		}
		switch (c) {
			case 'h':
				help = true;
				break;

			case 'E':
				logEn = true;
				break;

			case 'D':
				logDis = true;
				break;

			default:
				psh_dmesg_help(argv[0]);
				return EXIT_FAILURE;
		}
	}

	if (help) {
		psh_dmesg_help(argv[0]);
		return EXIT_SUCCESS;
	}

	if (logDis || logEn) {
		if (logDis && logEn) {
			fprintf(stderr, "dmesg: Invalid options.\n");
			return EXIT_FAILURE;
		}

		return psh_kmsgctrl(logEn);
	}

	int fd = open(_PATH_KLOG, O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		fprintf(stderr, "dmesg: Fail to open %s: %s\n", _PATH_KLOG, strerror(errno));
		return EXIT_FAILURE;
	}

	for (;;) {
		char buf[256];
		ssize_t n = read(fd, buf, sizeof(buf));
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
