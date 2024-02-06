/*
 * Phoenix-RTOS
 *
 * hd - prints file contents in hexadecimal and ascii representation
 *
 * Copyright 2024 Phoenix Systems
 * Author: Gerard Swiderski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/minmax.h>
#include <sys/stat.h>

#include "../psh.h"

#define LINE_WIDTH 16u


static void psh_hd_info(void)
{
	printf("dumps file contents in hexadecimal and ascii representation");
}


static void psh_hd_usage(const char *name)
{
	printf("Usage: %s -s offset -n length [filename]\n", name);
}


static void psh_hexdump(const uint8_t *data, size_t ofs, size_t len)
{
	while (len != 0u) {
		size_t toprint = min(len, LINE_WIDTH);

		printf("%08zx ", (size_t)ofs);

		for (size_t i = 0u; i < toprint; i++) {
			printf(((i & 7u) == 0u) ? " %02x " : "%02x ", data[i]);
		}

		for (size_t i = toprint; i < LINE_WIDTH; i++) {
			printf(((i & 7u) == 0u) ? "    " : "   ");
		}

		printf(" |");

		for (size_t i = 0u; i < toprint; i++) {
			printf("%c", (isprint(data[i]) != 0) ? (int)data[i] : (int)'.');
		}

		puts("|");

		data += toprint;
		ofs += toprint;
		len -= toprint;
	}
}


static int psh_hd(int argc, char **argv)
{
	uint8_t buffer[2u * LINE_WIDTH];
	struct stat st;
	char *filename = NULL;
	size_t len = SIZE_MAX;
	off_t ofs = 0;

	for (;;) {
		char *ptr = NULL;
		int opt = getopt(argc, argv, "hn:s:");
		if (opt == -1) {
			break;
		}
		switch (opt) {
			case 's':
				ofs = strtoul(optarg, &ptr, 0);
				break;

			case 'n':
				len = strtoul(optarg, &ptr, 0);
				break;

			case 'h':
				psh_hd_usage(argv[0]);
				return EXIT_SUCCESS;

			default:
				psh_hd_usage(argv[0]);
				return EXIT_FAILURE;
		}

		if ((optarg == ptr) || ((ptr != NULL) && ((*ptr != '\0') || (isdigit(*optarg) == 0)))) {
			fprintf(stderr, "%s: invalid value\n", argv[0]);
			return EXIT_FAILURE;
		}
	}

	if (optind < argc) {
		filename = argv[optind];
	}

	if (len != 0u) {
		int fd = (filename != NULL) ? open(filename, O_RDONLY) : STDIN_FILENO;
		filename = (filename != NULL) ? filename : "stdin";

		if ((fd < 0)) {
			fprintf(stderr, "%s: %s: %s\n", argv[0], filename, strerror(errno));
			return EXIT_FAILURE;
		}

		if ((fstat(fd, &st) == 0) && (S_ISDIR(st.st_mode) != 0)) {
			fprintf(stderr, "%s: %s: %s\n", argv[0], filename, strerror(EISDIR));
			return EXIT_FAILURE;
		}

		if (ofs > 0) {
			if (lseek(fd, ofs, SEEK_SET) < 0) {
				fprintf(stderr, "%s: %s\n", argv[0], strerror(errno));
				if (fd != STDIN_FILENO) {
					close(fd);
				}
				return EXIT_FAILURE;
			}
		}

		while ((len != 0u) && (psh_common.sigint == 0u)) {
			ssize_t toprint = read(fd, buffer, (size_t)min(len, sizeof(buffer)));

			if (toprint <= 0) {
				if (toprint == 0) {
					break;
				}
				if ((errno == EAGAIN) || (errno == EINTR)) {
					continue;
				}
				else {
					fprintf(stderr, "%s: %s\n", argv[0], strerror(errno));
					break;
				}
			}

			psh_hexdump(buffer, (size_t)ofs, (size_t)toprint);

			ofs += (off_t)toprint;
			len -= (size_t)toprint;
		}

		if (ofs != 0) {
			printf("%08zx\n", (size_t)ofs);
		}

		if (fd != STDIN_FILENO) {
			close(fd);
		}
	}

	return (len != 0u) ? EXIT_FAILURE : EXIT_SUCCESS;
}


static void __attribute__((constructor)) hd_registerapp(void)
{
	static psh_appentry_t app = { .name = "hd", .run = psh_hd, .info = psh_hd_info };
	psh_registerapp(&app);
}
