/*
 * Phoenix-RTOS
 *
 * cat - concatenate file(s) to standard output
 *
 * Copyright 2017, 2018, 2020-2022 Phoenix Systems
 * Author: Pawel Pisarczyk, Jan Sikorski, Lukasz Kosinski, Mateusz Niewiadomski, Aleksander Kaminski
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
#include <sys/stat.h>

#include "../psh.h"

#define SIZE_BUFF 1024

void psh_catinfo(void)
{
	printf("concatenate file(s) to standard output");
}

static void psh_cat_help(const char *prog)
{
	printf("Usage: %s [options] [files]\n", prog);
	printf("  -h:  shows this help message\n");
}


static inline int psh_cat_isExit(void)
{
	return ((psh_common.sigint != 0) || (psh_common.sigquit != 0) || (psh_common.sigstop != 0)) ? 1 : 0;
}


int psh_cat(int argc, char **argv)
{
	FILE *file;
	char *buff;
	size_t ret, len, wrote;
	int c, i, retval = EXIT_SUCCESS;
	struct stat sbuff;

	for (;;) {
		c = getopt(argc, argv, "h");
		if (c == -1) {
			break;
		}

		switch (c) {
			case 'h':
				psh_cat_help(argv[0]);
				return EXIT_SUCCESS;
			default:
				psh_cat_help(argv[0]);
				return EXIT_FAILURE;
		}
	}

	/* FIXME? We're operating on streams, there should be no need for big buffers */
	buff = malloc(SIZE_BUFF);
	if (buff == NULL) {
		perror("cat");
		return EXIT_FAILURE;
	}

	for (i = optind; (psh_cat_isExit() == 0) && (i < argc); ++i) {
		if (stat(argv[i], &sbuff) < 0) {
			fprintf(stderr, "cat: %s: %s\n", argv[i], strerror(errno));
			retval = EXIT_FAILURE;
			continue;
		}
		else if (S_ISDIR(sbuff.st_mode)) {
			fprintf(stderr, "cat: %s: %s\n", argv[i], strerror(EISDIR));
			retval = EXIT_FAILURE;
			continue;
		}
		else {
			file = fopen(argv[i], "r");
			if (file == NULL) {
				fprintf(stderr, "cat: %s: %s\n", argv[i], strerror(errno));
				retval = EXIT_FAILURE;
				continue;
			}

			while (psh_cat_isExit() == 0) {
				len = fread(buff, 1, SIZE_BUFF, file);
				if (len > 0) {
					wrote = 0;
					do {
						ret = fwrite(buff + wrote, 1, len, stdout);
						if (ret == 0) {
							retval = EXIT_FAILURE;
							break;
						}
						len -= ret;
						wrote += ret;
					} while (len > 0);
				}
				else {
					if (ferror(file) != 0) {
						retval = EXIT_FAILURE;
					}
					break;
				}
			}
			fclose(file);
		}
	}
	free(buff);

	return retval;
}


void __attribute__((constructor)) cat_registerapp(void)
{
	static psh_appentry_t app = { .name = "cat", .run = psh_cat, .info = psh_catinfo };
	psh_registerapp(&app);
}
