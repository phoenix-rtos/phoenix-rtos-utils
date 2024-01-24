/*
 * Phoenix-RTOS
 *
 * du - estimates file space usage
 *
 * Copyright 2024 Phoenix Systems
 * Author: Gerard Swiderski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <errno.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>   /* memset */
#include <unistd.h>   /* getopt */
#include <limits.h>   /* PATH_MAX */
#include <dirent.h>   /* readdir */
#include <sys/stat.h> /* lstat */

#include "../psh.h"

#define ALREADY_MAX 64
#define LEVELS_MAX  64


struct already {
	struct already *next;
	nlink_t nlink;
	ino_t inum;
	int dev;
};


static struct {
	struct already *already[ALREADY_MAX];
	char *dent;
	/* options */
	int levels;
	int8_t ki;
	bool silent;
	bool all;
	bool tot;
	bool crosschk;
} du_common;


/*
 * makePath - make the pathname from the directory name, and the directory entry,
 * placing it in out. If this would overflow return 0, otherwise length of path.
 */
static size_t makePath(size_t dlen, const char *f)
{
	char *cp = &du_common.dent[dlen];
	size_t length = strlen(f);

	if ((dlen + length) > (PATH_MAX - 2u)) {
		return 0;
	}

	if ((dlen != 0u) && (*(cp - 1) != '/')) {
		*(cp++) = '/';
	}

	memcpy(cp, f, length);
	cp += length;
	*cp = '\0';

	return cp - du_common.dent;
}


/*
 * isDone - have we encountered (dev, inum) before? Returns 1 for yes, 0 for no
 * and remembers (dev, inum, nlink).
 */
static int isDone(dev_t dev, ino_t inum, nlink_t nlink)
{
	struct already *ap;
	struct already **pap = &du_common.already[(int)inum % ALREADY_MAX];

	for (;;) {
		ap = *pap;
		if (ap == NULL) {
			break;
		}

		if ((ap->inum == inum) && (ap->dev == dev)) {
			ap->nlink--;
			if (ap->nlink == (nlink_t)0) {
				*pap = ap->next;
				free(ap);
			}
			return 1;
		}

		pap = &ap->next;
	}

	ap = (struct already *)malloc(sizeof(*ap));
	if (ap == NULL) {
		fprintf(stderr, "du: out of memory\n");
		return -1;
	}

	ap->next = NULL;
	ap->inum = inum;
	ap->dev = dev;
	ap->nlink = nlink - 1;
	*pap = ap;

	return 0;
}


static void freeAlreadyList(void)
{
	for (int i = 0; i < ALREADY_MAX; ++i) {
		struct already *list = du_common.already[i];
		while (list != NULL) {
			struct already *temp = list;
			list = list->next;
			free(temp);
		}
	}
}


static size_t roundPrefix(size_t v)
{
	switch (du_common.ki) {
		case 1: return (v + (size_t)999) / (size_t)1000;  /* kilo */
		case 2: return (v + (size_t)1023) / (size_t)1024; /* Kibi */
		default: break;
	}
	return v;
}


/*
 * doDirectory - process the directory.
 * Return the size (in bytes) of the directory and its descendants.
 */
static size_t doDirectory(size_t startPos, int curLevel, dev_t dev)
{
	DIR *dp;
	bool maybePrint;
	struct stat stbuf;
	size_t total = 0;
	struct dirent *entry;
	char *d = du_common.dent;

	if (lstat(d, &stbuf) < 0) {
		fprintf(stderr, "du: %s: %s\n", d, strerror(errno));
		return (size_t)0;
	}

	if ((stbuf.st_dev != dev) && (dev != 0) && (du_common.crosschk)) {
		return (size_t)0;
	}

	if (stbuf.st_size > (off_t)0) {
		total = (size_t)stbuf.st_size;
	}

	switch (stbuf.st_mode & S_IFMT) {
		case S_IFDIR:
			/*
			 * Directories should not be linked except to "." and "..", so this
			 * directory should not already have been done.
			 */
			maybePrint = !du_common.silent;
			dp = opendir(d);
			if (dp == NULL) {
				break;
			}

			for (;;) {
				du_common.dent[startPos] = '\0';
				entry = readdir(dp);
				if (entry == NULL) {
					break;
				}

				if (!((entry->d_name[0] == '.') && ((entry->d_name[1] == '\0') || ((entry->d_name[1] == '.') && (entry->d_name[2] == '\0'))))) {
					const size_t nextPos = makePath(startPos, entry->d_name);
					if (nextPos > 0u) {
						total += doDirectory(nextPos, curLevel - 1, stbuf.st_dev);
					}
				}
			}

			closedir(dp);
			break;

		case S_IFBLK:
		case S_IFCHR:
			/* st_size for special files is not related to blocks used. */
			total = (size_t)0;
			/* fall-through */

		default:
			if ((stbuf.st_nlink > (nlink_t)1) && (isDone(stbuf.st_dev, stbuf.st_ino, stbuf.st_nlink) != 0)) {
				return (size_t)0;
			}
			maybePrint = du_common.all;
			break;
	}


	if ((curLevel >= du_common.levels) || ((maybePrint) && (curLevel >= 0))) {
		printf("%zu\t%s\n", roundPrefix(total), d);
	}

	return total;
}


static void psh_du_info(void)
{
	printf("estimates file space usage");
}


static void psh_du_usage(void)
{
	fprintf(stderr, "Usage: du [-acsxkKh] [-d depth] [startdir]\n");
}


static int psh_du(int argc, char **argv)
{
	int opt;
	size_t total = 0;
	const char *startdir = ".";
	long int tmp = 0;

	memset(&du_common, 0, sizeof(du_common));
	du_common.levels = LEVELS_MAX;
	du_common.dent = calloc(1, PATH_MAX);
	if (du_common.dent == NULL) {
		fprintf(stderr, "du: out of memory\n");
		return EXIT_FAILURE;
	}

	for (;;) {
		opt = getopt(argc, argv, "acsxd:kKh");
		if (opt < 0) {
			break;
		}

		char *endp = optarg;
		switch (opt) {
			case 'a':
				du_common.all = 1;
				break;

			case 'c':
				du_common.tot = 1;
				break;

			case 's':
				du_common.silent = 1;
				break;

			case 'x':
				du_common.crosschk = 1;
				break;

			case 'd':
				tmp = strtol(optarg, &endp, 10);
				if ((endp == NULL) || (*endp != '\0') || (tmp < 0) || (tmp > LEVELS_MAX)) {
					fprintf(stderr, "du: invalid depth value\n");
					return EXIT_FAILURE;
				}
				du_common.levels = (int)tmp;
				break;

			case 'k':
				du_common.ki = 1; /* kilo */
				break;

			case 'K':
				du_common.ki = 2; /* kibi */
				break;

			case 'h': /* fall-through */
			default:
				psh_du_usage();
				return EXIT_FAILURE;
		}
	}

	do {
		if (optind < argc) {
			startdir = argv[optind++];
		}
		const size_t startPos = makePath(0, startdir);
		if (startPos > 0u) {
			total += doDirectory(startPos, du_common.levels, 0);
		}
	} while (optind < argc);

	freeAlreadyList();

	free(du_common.dent);

	if (du_common.tot) {
		printf("%zu\ttotal\n", roundPrefix(total));
	}

	return EXIT_SUCCESS;
}


static void __attribute__((constructor)) du_registerapp(void)
{
	static psh_appentry_t app = { .name = "du", .run = psh_du, .info = psh_du_info };
	psh_registerapp(&app);
}
