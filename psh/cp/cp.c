/*
 * Phoenix-RTOS
 *
 * cp - copy file
 *
 * Copyright 2022 Phoenix Systems
 * Author: Aleksander Kaminski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <libgen.h>
#include <sys/time.h>

#include "../psh.h"

#define SIZE_BUFF 256


static void psh_cpinfo(void)
{
	printf("copy file");
}


static void psh_cp_help(const char *prog)
{
	printf("Usage: %s [options] SOURCE TARGET\n", prog);
	printf("  -p:  preserve file attributes\n");
	printf("  -h:  shows this help message\n");
}


static int psh_cp(int argc, char **argv)
{
	FILE *src = NULL, *dst = NULL;
	int fdsrc, fddst;
	unsigned char buff[SIZE_BUFF];
	char *filename, *destpath = NULL, *attrpath;
	size_t destlen, filelen, cnt, wcnt;
	struct stat stat;
	int c, retval = EXIT_SUCCESS, preserve = 0;
	struct timeval times[2];

	while ((c = getopt(argc, argv, "hp")) != -1) {
		switch (c) {
			case 'h':
				psh_cp_help(argv[0]);
				return EXIT_SUCCESS;
			case 'p':
				preserve = 1;
				break;
			default:
				psh_cp_help(argv[0]);
				return EXIT_FAILURE;
		}
	}

	if (argc - optind != 2) {
		psh_cp_help(argv[0]);
		return EXIT_FAILURE;
	}

	do {
		fdsrc = open(argv[optind], O_RDONLY);
		if (fdsrc < 0) {
			retval = EXIT_FAILURE;
			perror("cp: could not open source file");
			break;
		}

		if (fstat(fdsrc, &stat) < 0) {
			close(fdsrc);
			retval = EXIT_FAILURE;
			perror("cp: stat failed");
			break;
		}

		if (!S_ISREG(stat.st_mode)) {
			close(fdsrc);
			retval = EXIT_FAILURE;
			fprintf(stderr, "cp: could not open source file: not a regular file\n");
			break;
		}

		src = fdopen(fdsrc, "r");
		if (src == NULL) {
			close(fdsrc);
			retval = EXIT_FAILURE;
			perror("cp: could not open source file");
			break;
		}

		if (setvbuf(src, NULL, _IONBF, 0) < 0) {
			retval = EXIT_FAILURE;
			fprintf(stderr, "cp: source setvbuff failure\n");
			break;
		}

		fddst = open(argv[optind + 1], O_WRONLY | O_CREAT, DEFFILEMODE);
		if (fddst < 0) {
			if (errno == EISDIR) {
				filename = basename(argv[optind]);
				destlen = strlen(argv[optind + 1]);
				filelen = strlen(filename);
				destpath = malloc(destlen + filelen + 2);
				if (destpath == NULL) {
					retval = EXIT_FAILURE;
					perror("cp");
					break;
				}

				strncpy(destpath, argv[optind + 1], destlen);
				destpath[destlen] = '/';
				strcpy(destpath + destlen + 1, filename);

				fddst = open(destpath, O_WRONLY | O_CREAT, DEFFILEMODE);

				if (fddst < 0) {
					retval = EXIT_FAILURE;
					perror("cp: could not open destination file");
					break;
				}
			}
			else {
				perror("cp: could not open destination file");
				retval = EXIT_FAILURE;
				break;
			}
		}

		dst = fdopen(fddst, "w");
		if (dst == NULL) {
			close(fddst);
			retval = EXIT_FAILURE;
			perror("cp: could not open destination file");
			break;
		}

		if (setvbuf(dst, NULL, _IONBF, 0) < 0) {
			retval = EXIT_FAILURE;
			fprintf(stderr, "cp: destination setvbuff failure\n");
			break;
		}

		while (1) {
			cnt = fread_unlocked(buff, sizeof(unsigned char), sizeof(buff), src);
			if (cnt == 0) {
				if (ferror(src) != 0) {
					retval = EXIT_FAILURE;
					fprintf(stderr, "cp: read failure\n");
				}
				break;
			}

			wcnt = fwrite_unlocked(buff, sizeof(unsigned char), cnt, dst);
			if (wcnt != cnt) {
				if (ferror(dst) != 0) {
					retval = EXIT_FAILURE;
					fprintf(stderr, "cp: write failure\n");
				}
				break;
			}
		}
	} while (0);

	if (src != NULL) {
		fclose(src);
	}

	if (dst != NULL) {
		fclose(dst);
	}

	if (retval == EXIT_SUCCESS && preserve != 0) {
		attrpath = (destpath == NULL) ? argv[optind + 1] : destpath;

		times[0].tv_sec = stat.st_atim.tv_sec;
		times[1].tv_sec = stat.st_mtim.tv_sec;
		times[0].tv_usec = 0;
		times[1].tv_usec = 0;

		if (chown(attrpath, stat.st_uid, stat.st_gid) < 0) {
			retval = EXIT_FAILURE;
			perror("cp: destination chown failed");
		}

		if (chmod(attrpath, stat.st_mode) < 0) {
			retval = EXIT_FAILURE;
			perror("cp: destination chmod failed");
		}

		if (utimes(attrpath, times) < 0) {
			retval = EXIT_FAILURE;
			perror("cp: destination utimes failed");
		}
	}

	free(destpath);

	return retval;
}


void __attribute__((constructor)) cp_registerapp(void)
{
	static psh_appentry_t app = { .name = "cp", .run = psh_cp, .info = psh_cpinfo };
	psh_registerapp(&app);
}
