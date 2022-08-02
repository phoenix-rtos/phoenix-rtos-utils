/*
 * Phoenix-RTOS
 *
 * cd - changes working directory
 *
 * Copyright 2022 Phoenix Systems
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
#include <getopt.h>

#include "../psh.h"


static void psh_cd_info(void)
{
	printf("changes the working directory");
}


static void psh_cd_help(const char *progname)
{
	printf("Usage: %s [directory]\n", progname);
}


static const char *psh_strerror(int errn)
{
	switch (errno) {
		case ENOENT: return "No such file or directory";
		case ENOTDIR: return "Not a directory";
		default: break;
	}

	return strerror(errno);
}


static int do_chdir(const char *dst)
{
	char *cwd;
	int res, print = 0;

	/* TODO: implement the search of the CDPATH if needed */

	if (dst == NULL) {
		dst = getenv("HOME");
		if (dst == NULL || dst[0] == '\0') {
			dst = "/";
		}
	}
	else if (dst[0] == '-' && dst[1] == '\0') {
		print = 1;
		dst = getenv("OLDPWD");
		if (dst == NULL) {
			fprintf(stderr, "cd: OLDPWD has not yet been set\n");
			return -1;
		}
	}

	cwd = getcwd(NULL, 0);
	res = chdir(dst);
	if (res < 0) {
		fprintf(stderr, "cd: %s - %s\n", dst, psh_strerror(errno));
		free(cwd);
		return -1;
	}

	if (print != 0) {
		puts(dst);
	}

	if (cwd != NULL) {
		setenv("OLDPWD", cwd, 1);
		free(cwd);
	}

	return 0;
}


static int psh_cd(int argc, char **argv)
{
	char *dst = NULL;
	int c, res = EXIT_FAILURE;

	while ((c = getopt(argc, argv, "h")) != -1) {
		switch (c) {
			case 'h':
				res = EXIT_SUCCESS;
				/* fall-through */
			default:
				psh_cd_help(argv[0]);
				return res;
		}
	}

	if (argc - optind > 1) {
		psh_cd_help(argv[0]);
		return EXIT_FAILURE;
	}

	if (argc - optind == 1) {
		dst = argv[optind];
	}

	return do_chdir(dst) < 0 ?
		EXIT_FAILURE :
		EXIT_SUCCESS;
}


void __attribute__((constructor)) cd_registerapp(void)
{
	static psh_appentry_t app = { .name = "cd", .run = psh_cd, .info = psh_cd_info };
	psh_registerapp(&app);
}
