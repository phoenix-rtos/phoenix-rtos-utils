/*
 * Phoenix-RTOS
 *
 * chmod - change file mode
 *
 * Copyright 2022-2024 Phoenix Systems
 * Author: Gerard Swiderski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>

#include "../psh.h"

#define USR_MODES (S_ISUID | S_IRWXU)
#define GRP_MODES (S_ISGID | S_IRWXG)
#define EXE_MODES (S_IXUSR | S_IXGRP | S_IXOTH)

#ifdef S_ISVTX
#define ALL_MODES (USR_MODES | GRP_MODES | S_IRWXO | S_ISVTX)
#else
#define ALL_MODES (USR_MODES | GRP_MODES | S_IRWXO)
#endif


#define PATH_MAX 512  // FIXME: shouldn't be here

static struct {
	char *symbolic;
	char path[PATH_MAX + 1];
	struct stat st;
	int rflag, errors;
	mode_t octal, u_mask;
} common;


static void psh_chmod_info(void)
{
	printf("changes file mode, chmod [-R] <mode> <file>...");
}


static int psh_chmod_help(void)
{
	fprintf(stderr, "Usage: chmod [-R] <mode> <file>...\n");
	return EXIT_FAILURE;
}


static int parsemode(char *symbolic, mode_t *pNewmode, mode_t oldmode)
{
	mode_t who, mask, newmode, tmpmask;
	char action;

	newmode = (oldmode & ALL_MODES);
	*pNewmode = newmode;

	while (*symbolic != '\0') {
		who = 0;
		for (; *symbolic != '\0'; symbolic++) {
			if (*symbolic == 'a') {
				who |= ALL_MODES;
				continue;
			}
			else if (*symbolic == 'u') {
				who |= USR_MODES;
				continue;
			}
			else if (*symbolic == 'g') {
				who |= GRP_MODES;
				continue;
			}
			else if (*symbolic == 'o') {
				who |= S_IRWXO;
				continue;
			}

			break;
		}

		if ((*symbolic == '\0') || (*symbolic == ',')) {
			return psh_chmod_help();
		}

		while (*symbolic != '\0') {
			if (*symbolic == ',') {
				break;
			}

			switch (*symbolic) {
				default: return psh_chmod_help();
				case '+': /* fall-through */
				case '-': /* fall-through */
				case '=': action = *symbolic++; break;
			}

			mask = 0;

			for (; *symbolic != '\0'; symbolic++) {
				if (*symbolic == 'u') {
					tmpmask = newmode & S_IRWXU;
					mask |= tmpmask | (tmpmask << 3) | (tmpmask << 6);
					symbolic++;
					break;
				}
				else if (*symbolic == 'g') {
					tmpmask = newmode & S_IRWXG;
					mask |= tmpmask | (tmpmask >> 3) | (tmpmask << 3);
					symbolic++;
					break;
				}
				else if (*symbolic == 'o') {
					tmpmask = newmode & S_IRWXO;
					mask |= tmpmask | (tmpmask >> 3) | (tmpmask >> 6);
					symbolic++;
					break;
				}
				else if (*symbolic == 'r') {
					mask |= S_IRUSR | S_IRGRP | S_IROTH;
					continue;
				}
				else if (*symbolic == 'w') {
					mask |= S_IWUSR | S_IWGRP | S_IWOTH;
					continue;
				}
				else if (*symbolic == 'x') {
					mask |= EXE_MODES;
					continue;
				}
				else if (*symbolic == 's') {
					mask |= S_ISUID | S_ISGID;
					continue;
				}
				else if (*symbolic == 'X') {
					if (S_ISDIR(oldmode) || (oldmode & EXE_MODES)) {
						mask |= EXE_MODES;
					}
					continue;
				}

#ifdef S_ISVTX
				else if (*symbolic == 't') {
					mask |= S_ISVTX;
					who |= S_ISVTX;
					continue;
				}
#endif

				break;
			}

			switch (action) {
				case '=':
					if (who) {
						newmode &= ~who;
					}
					else {
						newmode = 0;
					}
					/* fall-through */

				case '+':
					if (who) {
						newmode |= who & mask;
					}
					else {
						newmode |= mask & (~common.u_mask);
					}
					break;

				case '-':
					if (who) {
						newmode &= ~(who & mask);
					}
					else {
						newmode &= ~mask | common.u_mask;
					}
					break;
			}
		}

		if (*symbolic != '\0') {
			symbolic++;
		}
	}

	*pNewmode = newmode;

	return EXIT_SUCCESS;
}


static int do_chmod(char *name)
{
	mode_t mode;
	DIR *dirp;
	struct dirent *entp;
	char *namp;

	if (lstat(name, &common.st)) {
		perror(name);
		return EXIT_FAILURE;
	}

	if (S_ISLNK(common.st.st_mode) && (common.rflag != 0)) {
		return EXIT_SUCCESS;
	}

	if (common.symbolic != NULL) {
		if (parsemode(common.symbolic, &mode, common.st.st_mode) == EXIT_FAILURE) {
			return -1;
		}
	}
	else {
		mode = common.octal;
	}

	if (chmod(name, mode)) {
		perror(name);
		common.errors = 1;
	}
	else {
		common.errors = 0;
	}

	if (S_ISDIR(common.st.st_mode) && (common.rflag != 0)) {
		dirp = opendir(name);
		if (dirp == NULL) {
			perror(name);
			return EXIT_FAILURE;
		}

		if (name != common.path) {
			strncpy(common.path, name, PATH_MAX);
		}

		/* FIXME: use realloc for common.path, check if we fit into common.path, etc. */
		namp = common.path + strlen(common.path);
		*namp++ = '/';

		entp = readdir(dirp);
		while (entp != NULL) {
			if (entp->d_name[0] != '.' || (entp->d_name[1] && (entp->d_name[1] != '.' || entp->d_name[2]))) {
				strcpy(namp, entp->d_name); /* FIXME: strcpy */
				common.errors |= do_chmod(common.path);
			}

			entp = readdir(dirp);
		}
		closedir(dirp);
		*(--namp) = '\0';
	}

	return (common.errors != 0) ? EXIT_FAILURE : EXIT_SUCCESS;
}


static int psh_chmod(int argc, char **argv)
{
	int ret, exitCode = EXIT_SUCCESS;

	argc--;
	argv++;

	memset(&common, 0, sizeof(common));

	if ((argc != 0) && (strcmp(*argv, "-R") == 0)) {
		argc--;
		argv++;
		common.rflag = 1;
	}
	else {
		common.rflag = 0;
	}

	if (argc-- <= 0) {
		return psh_chmod_help();
	}

	common.symbolic = *argv++;
	if (argc == 0) {
		return psh_chmod_help();
	}

	if (isdigit(*common.symbolic)) {
		common.octal = 0;
		for (; isdigit(*common.symbolic); common.symbolic++) {
			common.octal = (common.octal << 3) | (*common.symbolic & 07);
		}

		if (*common.symbolic) {
			return psh_chmod_help();
		}

		common.octal &= ALL_MODES;
		common.symbolic = NULL;
	}
	else {
		common.u_mask = umask(0);
	}

	while (argc-- > 0) {
		ret = do_chmod(*argv++);
		if (ret < 0) {
			return EXIT_FAILURE;
		}
		else if (ret == EXIT_FAILURE) {
			exitCode = EXIT_FAILURE;
		}
	}

	return exitCode;
}


void __attribute__((constructor)) chmod_registerapp(void)
{
	static psh_appentry_t app = { .name = "chmod", .run = psh_chmod, .info = psh_chmod_info };
	psh_registerapp(&app);
}
