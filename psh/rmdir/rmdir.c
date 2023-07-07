/*
 * Phoenix-RTOS
 *
 * rmdir - remove empty directories
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

#include "../psh.h"


static void psh_rmdir_info(void)
{
	printf("remove empty directories");
}


static void psh_rmdir_usage(void)
{
	printf("Usage: rmdir [-p] DIRECTORY...\n");
}


static int removedir(char *name, int parents)
{
	char *end = name + strlen(name) - (size_t)1;
	int suppress = 0;
	int err = -1;

	while (name[0] != '\0') {
		while ((end >= name) && (*end == '/')) {
			*(end--) = '\0';
		}

		if (name[0] != '\0') {
			err = rmdir(name);
			end = strrchr(name, '/');
			if ((err != 0) || (end == NULL) || (parents == 0)) {
				break;
			}

			suppress = 1;
		}
	}


	return ((err != 0) && (suppress == 0));
}


static int psh_rmdir(int argc, char **argv)
{
	char *dirname;
	int i, parent = 0, ret = EXIT_SUCCESS;

	if ((argc > 1) && (argv[1][0] == '-') && (argv[1][1] == 'p') && (argv[1][2] == '\0')) {
		parent = 1;
	}

	if ((parent + 1) == argc) {
		psh_rmdir_usage();
		return EXIT_FAILURE;
	}

	for (i = parent + 1; i < argc; i++) {
		if (argv[i][0] != '-') {
			dirname = strdup(argv[i]);
			if (dirname == NULL) {
				fprintf(stderr, "rmdir: out of memory\n");
				return EXIT_FAILURE;
			}
			if (removedir(dirname, parent) != 0) {
				fprintf(stderr, "rmdir: cannot remove directory %s: %s\n", argv[i], strerror(errno));
				ret = EXIT_FAILURE;
			}
			free(dirname);
		}
		else {
			fprintf(stderr, "rmdir: usage error\n");
			return EXIT_FAILURE;
		}
	}

	return ret;
}


static void __attribute__((constructor)) rmdir_registerapp(void)
{
	static psh_appentry_t app = { .name = "rmdir", .run = psh_rmdir, .info = psh_rmdir_info };
	psh_registerapp(&app);
}
