/*
 * Phoenix-RTOS
 *
 * rm - unlink files or remove empty directories
 *
 * Copyright 2023 Phoenix Systems
 * Author: Gerard Swiderski
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


static void psh_rm_info(void)
{
	printf("unlink files or remove empty directories");
}


static void psh_rm_usage(void)
{
	printf("Usage: rm [-d] FILE...\n");
}


static int psh_rm(int argc, char **argv)
{
	struct stat stbuf;
	int i, err, rdir = 0, ret = EXIT_SUCCESS;

	if ((argc > 1) && (argv[1][0] == '-') && (argv[1][1] == 'd') && (argv[1][2] == '\0')) {
		rdir = 1;
	}

	if ((rdir + 1) == argc) {
		psh_rm_usage();
		return EXIT_FAILURE;
	}

	for (i = rdir + 1; i < argc; i++) {
		if (argv[i][0] == '-') {
			fprintf(stderr, "rm: usage error\n");
			return EXIT_FAILURE;
		}

		err = lstat(argv[i], &stbuf);
		if (err == 0) {
			if (S_ISDIR(stbuf.st_mode)) {
				if (rdir != 0) {
					err = rmdir(argv[i]);
				}
				else {
					fprintf(stderr, "rm: cannot remove '%s' is a directory\n", argv[i]);
					ret = EXIT_FAILURE;
				}
			}
			else {
				err = unlink(argv[i]);
			}
		}

		if (err != 0) {
			fprintf(stderr, "rm: could not remove '%s': %s\n", argv[i], strerror(errno));
			ret = EXIT_FAILURE;
		}
	}

	return ret;
}


static void __attribute__((constructor)) rm_registerapp(void)
{
	static psh_appentry_t app = { .name = "rm", .run = psh_rm, .info = psh_rm_info };
	psh_registerapp(&app);
}
