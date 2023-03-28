/*
 * Phoenix-RTOS
 *
 * ln - make links between files
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


static void psh_ln_info(void)
{
	printf("make links between files");
}


static void psh_ln_usage(void)
{
	printf(
		"Usage: ln [-s] TARGET LINK_NAME\n"
		"       ln TARGET... LINK_NAME\n");
}


static int isadir(char *name)
{
	struct stat statbuf;

	if (stat(name, &statbuf) < 0) {
		return -1;
	}

	return S_ISDIR(statbuf.st_mode);
}


static char *pathconcat(char *dirname, char *filename, char **pBuf)
{
	char *buf;
	char *cp;
	size_t len;

	if ((dirname == NULL) || (*dirname == '\0')) {
		return filename;
	}

	cp = strrchr(filename, '/');
	if (cp != NULL) {
		filename = cp + 1;
	}

	len = strlen(dirname) + 1u + strlen(filename) + 1u;
	buf = (char *)realloc(*pBuf, len);
	if (buf != NULL) {
		cp = stpcpy(buf, dirname);
		cp = stpcpy(cp, "/");
		stpcpy(cp, filename);
		*pBuf = buf;
	}

	return buf;
}


static int psh_ln(int argc, char **argv)
{
	char *lastarg;
	char *srcname;
	char *dstname;
	char *buf = NULL;
	int dirflag;
	int ret = EXIT_SUCCESS;

	if (argc < 3) {
		fprintf(stderr, "ln: invalid arguments\n");
		psh_ln_usage();
		return EXIT_FAILURE;
	}

	if (argv[1][0] == '-') {

		/* Symbolic link */
		if (strcmp(argv[1], "-s") == 0) {
			if (argc != 4) {
				fprintf(stderr, "ln: wrong number of arguments for symbolic link\n");
				return EXIT_FAILURE;
			}

			if (symlink(argv[2], argv[3]) < 0) {
				fprintf(stderr, "ln: failed to create symbolic link '%s' -> '%s': %s\n",
					argv[2], argv[3], strerror(errno));
				return EXIT_FAILURE;
			}
			return EXIT_SUCCESS;
		}

		fprintf(stderr, "ln: unknown option %s\n", argv[1]);
		psh_ln_usage();
		return EXIT_FAILURE;
	}

	/* Hard links */

	lastarg = argv[argc - 1];
	dirflag = isadir(lastarg);

	if ((argc > 3) && (dirflag < 1)) {
		fprintf(stderr, "ln: '%s' not a directory\n", lastarg);
		return EXIT_FAILURE;
	}

	while (argc-- > 2) {
		srcname = *(++argv);
		if (access(srcname, 0) < 0) {
			fprintf(stderr, "ln: unable to access '%s': %s\n", srcname, strerror(errno));
			continue;
		}

		dstname = lastarg;
		if (dirflag == 1) {
			dstname = pathconcat(dstname, srcname, &buf);
			if (dstname == NULL) {
				fprintf(stderr, "ln: out of memory\n");
				ret = EXIT_FAILURE;
				break;
			}
		}

		if (link(srcname, dstname) < 0) {
			fprintf(stderr, "ln: failed to create hard link '%s' -> '%s': %s\n", srcname, dstname, strerror(errno));
			continue;
		}
	}

	free(buf);

	return ret;
}


static void __attribute__((constructor)) ln_registerapp(void)
{
	static psh_appentry_t app = { .name = "ln", .run = psh_ln, .info = psh_ln_info };
	psh_registerapp(&app);
}
