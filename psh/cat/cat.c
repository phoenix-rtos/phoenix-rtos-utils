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

	while ((c = getopt(argc, argv, "h")) != -1) {
		switch (c) {
			case 'h':
				psh_cat_help(argv[0]);
				break;
			default:
				psh_cat_help(argv[0]);
				retval = EXIT_FAILURE;
				break;
		}

		return retval;
	}

	/* FIXME? We're operating on streams, there should be no need for big buffers */
	if ((buff = malloc(SIZE_BUFF)) == NULL) {
		fprintf(stderr, "cat: out of memory\n");
		return EXIT_FAILURE;
	}

	for (i = optind; (psh_cat_isExit() == 0) && (i < argc); ++i) {
		if ((file = fopen(argv[i], "r")) == NULL) {
			fprintf(stderr, "cat: %s no such file\n", argv[i]);
			retval = EXIT_FAILURE;
		}
		else {
			while (psh_cat_isExit() == 0) {
				if ((len = fread(buff, 1, SIZE_BUFF, file)) > 0) {
					wrote = 0;
					do {
						if ((ret = fwrite(buff + wrote, 1, len, stdout)) == 0) {
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
