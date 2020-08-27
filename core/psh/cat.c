/*
 * Phoenix-RTOS
 *
 * cat - concatenate file(s) to standard output
 *
 * Copyright 2017, 2018, 2020 Phoenix Systems
 * Author: Pawel Pisarczyk, Jan Sikorski, Lukasz Kosinski
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

#include "psh.h"


static void psh_cat_help(const char *prog)
{
	printf("Usage: %s [options] [files]\n", prog);
	printf("  -h:  shows this help message\n");
}


int psh_cat(int argc, char **argv)
{
	FILE *file;
	char *buff;
	size_t ret;
	int c;

	while ((c = getopt(argc, argv, "h")) != -1) {
		switch (c) {
		case 'h':
		default:
			psh_cat_help(argv[0]);
			return EOK;
		}
	}

	if ((buff = (char *)malloc(1024)) == NULL) {
		printf("cat: out of memory\n");
		return -ENOMEM;
	}

	for (; !psh_common.sigint && !psh_common.sigquit && !psh_common.sigstop && (optind < argc); optind++) {
		if ((file = fopen(argv[optind], "r")) == NULL) {
			printf("cat: %s no such file\n", argv[optind]);
		}
		else {
			while (!psh_common.sigint && !psh_common.sigquit && !psh_common.sigstop && ((ret = fread(buff, 1, 1024, file)) > 0))
				fwrite(buff, 1, ret, stdout);
			fclose(file);
		}
	}
	free(buff);

	return EOK;
}
