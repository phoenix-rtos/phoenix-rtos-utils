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
#include <limits.h> /* PATH_MAX */

#include "../psh.h"

#define USR_MODES (S_ISUID | S_IRWXU)
#define GRP_MODES (S_ISGID | S_IRWXG)
#define EXE_MODES (S_IXUSR | S_IXGRP | S_IXOTH)

#ifdef S_ISVTX
#define ALL_MODES (USR_MODES | GRP_MODES | S_IRWXO | S_ISVTX)
#else
#define ALL_MODES (USR_MODES | GRP_MODES | S_IRWXO)
#endif


static struct {
	char *symbolic;
	char *path;
	struct stat st;
	int rflag;
	int errors;
	mode_t octal;
	mode_t u_mask;
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
	mode_t who;
	mode_t mask;
	mode_t newmode;
	mode_t tmpmask;
	char action;

	newmode = (oldmode & ALL_MODES);
	*pNewmode = newmode;

	while (*symbolic != '\0') {
		who = 0;
		for (; *symbolic != '\0'; symbolic++) {
			char c = *symbolic;
			if (c == 'a') {
				who |= ALL_MODES;
			}
			else if (c == 'u') {
				who |= USR_MODES;
			}
			else if (c == 'g') {
				who |= GRP_MODES;
			}
			else if (c == 'o') {
				who |= S_IRWXO;
			}
			else {
				break;
			}
		}

		if ((*symbolic == '\0') || (*symbolic == ',')) {
			return psh_chmod_help();
		}

		while (*symbolic != '\0') {
			if (*symbolic == ',') {
				break;
			}

			switch (*symbolic) {
				case '+': /* fall-through */
				case '-': /* fall-through */
				case '=': action = *symbolic++; break;
				default: return psh_chmod_help();
			}

			mask = 0;

			for (; *symbolic != '\0'; symbolic++) {
				char c = *symbolic;
				if (c == 'u') {
					tmpmask = newmode & S_IRWXU;
					mask |= tmpmask | (tmpmask << 3) | (tmpmask << 6);
					break;
				}
				else if (c == 'g') {
					tmpmask = newmode & S_IRWXG;
					mask |= tmpmask | (tmpmask >> 3) | (tmpmask << 3);
					break;
				}
				else if (c == 'o') {
					tmpmask = newmode & S_IRWXO;
					mask |= tmpmask | (tmpmask >> 3) | (tmpmask >> 6);
					break;
				}
				else if (c == 'r') {
					mask |= S_IRUSR | S_IRGRP | S_IROTH;
				}
				else if (c == 'w') {
					mask |= S_IWUSR | S_IWGRP | S_IWOTH;
				}
				else if (c == 'x') {
					mask |= EXE_MODES;
				}
				else if (c == 's') {
					mask |= S_ISUID | S_ISGID;
				}
				else if (c == 'X') {
					if (S_ISDIR(oldmode) || (oldmode & EXE_MODES)) {
						mask |= EXE_MODES;
					}
				}
#ifdef S_ISVTX
				else if (c == 't') {
					mask |= S_ISVTX;
					who |= S_ISVTX;
				}
#endif
				else {
					break;
				}
			}

			switch (action) {
				case '=':
					if (who != (mode_t)0) {
						newmode &= ~who;
						newmode |= who & mask;
					}
					else {
						newmode = mask & (~common.u_mask);
					}
					break;

				case '+':
					if (who != (mode_t)0) {
						newmode |= who & mask;
					}
					else {
						newmode |= mask & (~common.u_mask);
					}
					break;

				case '-':
					if (who != (mode_t)0) {
						newmode &= ~(who & mask);
					}
					else {
						newmode &= ~mask | common.u_mask;
					}
					break;

				default:
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

	if (lstat(name, &common.st) != 0) {
		perror(name);
		return EXIT_FAILURE;
	}

	if (S_ISLNK(common.st.st_mode) && (common.rflag != 0)) {
		return EXIT_SUCCESS;
	}

	if (common.symbolic != NULL) {
		if (parsemode(common.symbolic, &mode, common.st.st_mode) == EXIT_FAILURE) {
			fprintf(stderr, "chmod: wrong MODE set\n");
			return -1;
		}
	}
	else {
		mode = common.octal;
	}

	if (chmod(name, mode) != 0) {
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
			common.path[PATH_MAX - 1] = '\0';
		}

		namp = common.path + strlen(common.path);
		if ((namp - common.path) < (PATH_MAX - 1)) {
			*namp++ = '/';
		}
		else {
			closedir(dirp);
			fprintf(stderr, "chmod: MAX_PATH length exceeded\n");
			return EXIT_FAILURE;
		}

		entp = readdir(dirp);
		while (entp != NULL) {
			/* skip . and .. */
			if ((entp->d_name[0] != '.') || ((entp->d_name[1] != '\0') && ((entp->d_name[1] != '.') || (entp->d_name[2] != '\0')))) {
				strncpy(namp, entp->d_name, PATH_MAX - (namp - common.path) - 1);
				namp[PATH_MAX - (namp - common.path)] = '\0';
				common.errors |= do_chmod(common.path);
			}

			entp = readdir(dirp);
		}
		closedir(dirp);
	}

	return (common.errors != 0) ? EXIT_FAILURE : EXIT_SUCCESS;
}


static int psh_chmod(int argc, char **argv)
{
	int ret;
	int exitCode = EXIT_SUCCESS;

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

	if (isdigit(*common.symbolic) != 0) {
		common.octal = 0;
		for (; isdigit(*common.symbolic); common.symbolic++) {
			common.octal = (common.octal << 3) | (*common.symbolic & 7);
		}

		if (*common.symbolic != '\0') {
			return psh_chmod_help();
		}

		common.octal &= ALL_MODES;
		common.symbolic = NULL;
	}
	else {
		common.u_mask = umask(0);
	}

	common.path = calloc(1, PATH_MAX);
	if (common.path == NULL) {
		return EXIT_FAILURE;
	}

	while (argc-- > 0) {
		ret = do_chmod(*argv++);
		if (ret < 0) {
			exitCode = EXIT_FAILURE;
			break;
		}
		if (ret == EXIT_FAILURE) {
			exitCode = EXIT_FAILURE;
			/* no break, continue */
		}
	}

	free(common.path);

	return exitCode;
}


static void __attribute__((constructor)) chmod_registerapp(void)
{
	static psh_appentry_t app = { .name = "chmod", .run = psh_chmod, .info = psh_chmod_info };
	psh_registerapp(&app);
}
