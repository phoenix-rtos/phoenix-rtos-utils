/*
 * Phoenix-RTOS
 *
 * ls - lists files and directories
 *
 * Copyright 2017, 2018, 2020, 2021 Phoenix Systems
 * Author: Maciej Purski, Lukasz Kosinski, Mateusz Niewiadomski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <dirent.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/minmax.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "../psh.h"


#define DIR_COLOR "\033[34m"    /* Blue */
#define EXE_COLOR "\033[32m"    /* Green */
#define SYM_COLOR "\033[36m"    /* Cyan */
#define DEV_COLOR "\033[33;40m" /* Yellow with black bg */


enum { MODE_NORMAL, MODE_ONEPERLINE, MODE_LONG };


typedef struct {
	char *name;
	size_t namelen;
	size_t memlen;
	struct stat stat;
	struct passwd *pw;
	struct group *gr;
	uint32_t d_type;
} fileinfo_t;


static struct {
	struct winsize ws;
	fileinfo_t *files;
	size_t fileinfosz;
	int *odir;
	int mode;
	int all;
	int reverse;
	int dir;
	int (*cmp)(const void *, const void *);
} psh_ls_common;


static int psh_ls_cmpname(const void *t1, const void *t2)
{
	return strcasecmp(((fileinfo_t *)t1)->name, ((fileinfo_t *)t2)->name) * psh_ls_common.reverse;
}


static int psh_ls_cmpmtime(const void *t1, const void *t2)
{
	return (((fileinfo_t *)t2)->stat.st_mtime - ((fileinfo_t *)t1)->stat.st_mtime) * psh_ls_common.reverse;
}


static int psh_ls_cmpsize(const void *t1, const void *t2)
{
	return (((fileinfo_t *)t2)->stat.st_size - ((fileinfo_t *)t1)->stat.st_size) * psh_ls_common.reverse;
}


static void psh_ls_help(void)
{
	printf("usage: ls [options] [files]\n");
	printf("  -1:  one entry per line\n");
	printf("  -a:  do not ignore entries starting with .\n");
	printf("  -d:  list directories themselves, not their contents\n");
	printf("  -f:  do not sort\n");
	printf("  -h:  prints help\n");
	printf("  -l:  long listing format\n");
	printf("  -r:  sort in reverse order\n");
	printf("  -S:  sort by file size, largest first\n");
	printf("  -t:  sort by time, newest first\n");
}


static size_t *psh_ls_computerows(size_t *rows, size_t *cols, size_t nfiles)
{
	fileinfo_t *files = psh_ls_common.files;
	size_t *colsz, sum = 0, nrows = 1, ncols = nfiles;
	unsigned int i, col;

	/* Estimate lower bound of nrows */
	for (i = 0; i < nfiles; i++)
		sum += min(files[i].namelen, psh_ls_common.ws.ws_col - 1);

	nrows = sum / psh_ls_common.ws.ws_col + 1;
	ncols = nfiles / nrows + 1;

	if ((colsz = malloc(ncols * sizeof(size_t))) == NULL) {
		fprintf(stderr, "ls: out of memory\n");
		return NULL;
	}

	for (; nrows <= nfiles; nrows++) {
		ncols = nfiles / nrows + !!(nfiles % nrows);
		for (i = 0; i < ncols; i++)
			colsz[i] = 0;

		/* Compute widths of each column */
		for (i = 0; i < nfiles; i++) {
			col = i / nrows;
			colsz[col] = max(colsz[col], min(files[i].namelen + 2, psh_ls_common.ws.ws_col - 1));
		}
		colsz[ncols - 1] -= 2;

		/* Compute all columns but last */
		for (sum = 0, col = 0; col < ncols; col++)
			sum += colsz[col];

		if (sum < psh_ls_common.ws.ws_col)
			break;
	}

	*rows = nrows;
	*cols = ncols;

	return colsz;
}


static void psh_ls_printfile(fileinfo_t *file, int width)
{
	unsigned char iscolor = 1;
	if (S_ISREG(file->stat.st_mode) && file->stat.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) {
		printf(EXE_COLOR);
	}
	else if (S_ISDIR(file->stat.st_mode)) {
		printf(DIR_COLOR);
	}
	else if (S_ISCHR(file->stat.st_mode) || S_ISBLK(file->stat.st_mode)) {
		printf(DEV_COLOR);
	}
	else if (S_ISLNK(file->stat.st_mode)) {
		printf(SYM_COLOR);
	}
	else {
		iscolor = 0;
	}

	printf("%-*s", width, file->name);
	if (iscolor) {
		printf("\033[0m");
	}
}


static int psh_ls_copyname(fileinfo_t *file, const char *name)
{
	size_t namelen = strlen(name);
	char *rname;

	if (file->memlen <= namelen) {
		if ((rname = realloc(file->name, namelen + 1)) == NULL) {
			fprintf(stderr, "ls: out of memory\n");
			return -ENOMEM;
		}
		file->memlen = namelen + 1;
		file->name = rname;
	}

	strcpy(file->name, name);
	file->namelen = namelen;

	return EOK;
}


static int psh_ls_readentry(fileinfo_t *file, struct dirent *dir, const char *path)
{
	size_t pathlen = strlen(path);
	char *fullname;
	int ret;

	if ((ret = psh_ls_copyname(file, dir->d_name)) < 0)
		return ret;

	if ((fullname = malloc((file->namelen + pathlen + 2) * sizeof(char))) == NULL) {
		fprintf(stderr, "ls: out of memory\n");
		return -ENOMEM;
	}
	strcpy(fullname, path);

	if (fullname[pathlen - 1] != '/') {
		fullname[pathlen] = '/';
		strcpy(fullname + pathlen + 1, dir->d_name);
	}
	else {
		strcpy(fullname + pathlen, dir->d_name);
	}

	if ((ret = lstat(fullname, &file->stat)) < 0) {
		fprintf(stderr, "ls: can't stat file %s\n", dir->d_name);
		free(fullname);
		return ret;
	}

	if (psh_ls_common.mode == MODE_LONG) {
		file->pw = getpwuid(file->stat.st_uid);
		file->gr = getgrgid(file->stat.st_gid);
	}

	file->d_type = dir->d_type;
	free(fullname);

	return EOK;
}


static int psh_ls_readfile(fileinfo_t *file, char *path)
{
	int ret;

	if ((ret = psh_ls_copyname(file, path)) < 0)
		return ret;

	if (psh_ls_common.mode == MODE_LONG) {
		file->pw = getpwuid(file->stat.st_uid);
		file->gr = getgrgid(file->stat.st_gid);
	}

	return EOK;
}


static unsigned int psh_ls_numplaces(unsigned int n)
{
	unsigned int r = 1;

	while (n /= 10)
		r++;

	return r;
}


static void psh_ls_printlong(size_t nfiles)
{
	fileinfo_t *files = psh_ls_common.files;
	char perms[11], buff[80];
	int linksz = 1;
	int usersz = 3;
	int grpsz = 3;
	int sizesz = 1;
	int daysz = 1;
	unsigned int i, j;
	struct tm t;
	int currentYear = 0;
	time_t currentTime = time(NULL);

	if (localtime_r(&currentTime, &t) != NULL) {
		currentYear = t.tm_year;
	}

	for (i = 0; i < nfiles; i++) {
		linksz = max(psh_ls_numplaces(files[i].stat.st_nlink), linksz);
		sizesz = max(psh_ls_numplaces(files[i].stat.st_size), sizesz);

		if (files[i].pw != NULL)
			usersz = max(strlen(files[i].pw->pw_name), usersz);

		if (files[i].gr != NULL)
			grpsz = max(strlen(files[i].gr->gr_name), grpsz);

		localtime_r(&files[i].stat.st_mtime, &t);
		if (t.tm_mday >= 10)
			daysz = 2;
	}

	for (i = 0; i < nfiles; i++) {
		for (j = 0; j < 10; j++)
			perms[j] = '-';
		perms[10] = '\0';

		if (S_ISDIR(files[i].stat.st_mode))
			perms[0] = 'd';
		else if (S_ISCHR(files[i].stat.st_mode))
			perms[0] = 'c';
		else if (S_ISBLK(files[i].stat.st_mode))
			perms[0] = 'b';
		else if (S_ISLNK(files[i].stat.st_mode))
			perms[0] = 'l';
		else if (S_ISFIFO(files[i].stat.st_mode))
			perms[0] = 'p';
		else if (S_ISSOCK(files[i].stat.st_mode))
			perms[0] = 's';

		if (files[i].stat.st_mode & S_IRUSR)
			perms[1] = 'r';
		if (files[i].stat.st_mode & S_IWUSR)
			perms[2] = 'w';
		if (files[i].stat.st_mode & S_IXUSR)
			perms[3] = 'x';
		if (files[i].stat.st_mode & S_IRGRP)
			perms[4] = 'r';
		if (files[i].stat.st_mode & S_IWGRP)
			perms[5] = 'w';
		if (files[i].stat.st_mode & S_IXGRP)
			perms[6] = 'x';
		if (files[i].stat.st_mode & S_IROTH)
			perms[7] = 'r';
		if (files[i].stat.st_mode & S_IWOTH)
			perms[8] = 'w';
		if (files[i].stat.st_mode & S_IXOTH)
			perms[9] = 'x';

		files[i].gr = getgrgid(files[i].stat.st_gid);
		localtime_r(&files[i].stat.st_mtime, &t);
		strftime(buff, 80, "%b ", &t);
		sprintf(buff + 4, "%*d ", daysz, t.tm_mday);
		if (t.tm_year == currentYear) {
			strftime(buff + 5 + daysz, 75 - daysz, "%H:%M", &t);
		}
		else {
			snprintf(buff + 5 + daysz, 75 - daysz, "%5d", t.tm_year + 1900);
		}

		printf("%s %*d ", perms, linksz, files[i].stat.st_nlink);
		printf("%-*s ", usersz, (files[i].pw != NULL) ? files[i].pw->pw_name : "---");
		printf("%-*s ", grpsz, (files[i].gr != NULL) ? files[i].gr->gr_name : "---");
		printf("%*lld %s ", sizesz, files[i].stat.st_size, buff);

		psh_ls_printfile(&files[i], files[i].namelen);
		putchar('\n');
	}
}


static int psh_ls_printmultiline(size_t nfiles)
{
	fileinfo_t *files = psh_ls_common.files;
	size_t ncols, nrows, *colsz;
	unsigned int row, col, idx;

	if ((colsz = psh_ls_computerows(&nrows, &ncols, nfiles)) == NULL)
		return -ENOMEM;

	for (row = 0; row < nrows; row++) {
		for (col = 0; col < ncols; col++) {
			if ((idx = col * nrows + row) >= nfiles)
				continue;
			psh_ls_printfile(&files[idx], max(files[idx].namelen, min(colsz[col], psh_ls_common.ws.ws_col)));
		}
		putchar('\n');
	}
	free(colsz);

	return EOK;
}


static int psh_ls_printfiles(size_t nfiles)
{
	unsigned int i;
	int ret = EOK;

	if (psh_ls_common.mode == MODE_LONG) {
		psh_ls_printlong(nfiles);
	}
	else if (psh_ls_common.mode == MODE_ONEPERLINE) {
		for (i = 0; i < nfiles; i++) {
			psh_ls_printfile(&psh_ls_common.files[i], psh_ls_common.files[i].namelen);
			putchar('\n');
		}
	}
	else {
		ret = psh_ls_printmultiline(nfiles);
	}

	return ret;
}


static int psh_ls_expandbuff(size_t size)
{
	fileinfo_t *rptr;
	size_t i;

	if ((rptr = realloc(psh_ls_common.files, size * sizeof(fileinfo_t))) == NULL) {
		fprintf(stderr, "ls: out of memory\n");
		return -ENOMEM;
	}

	psh_ls_common.files = rptr;
	for (i = psh_ls_common.fileinfosz; i < size; i++) {
		psh_ls_common.files[i].memlen = 0;
		psh_ls_common.files[i].name = NULL;
	}
	psh_ls_common.fileinfosz = size;

	return EOK;
}


static void psh_ls_free(void)
{
	size_t i;

	if (psh_ls_common.files != NULL) {
		for (i = 0; i < psh_ls_common.fileinfosz; i++)
			free(psh_ls_common.files[i].name);
		free(psh_ls_common.files);
	}
	free(psh_ls_common.odir);
}


void psh_lsinfo(void)
{
	printf("lists files in the namespace");
}


int psh_ls(int argc, char **argv)
{
	unsigned int i, npaths = 0;
	int c, nfiles = 0, ret = EOK;
	char **paths = NULL;
	char *currdir = NULL;
	struct dirent *dir;
	const char *path;
	DIR *stream;

	/* In case of ioctl fail set default window size */
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &psh_ls_common.ws) != 0) {
		psh_ls_common.ws.ws_col = 80;
		psh_ls_common.ws.ws_row = 25;
	}

	psh_ls_common.fileinfosz = 0;
	psh_ls_common.files = NULL;
	psh_ls_common.odir = NULL;
	psh_ls_common.cmp = psh_ls_cmpname;
	psh_ls_common.reverse = 1;
	psh_ls_common.all = 0;
	psh_ls_common.dir = 0;
	psh_ls_common.mode = MODE_NORMAL;

	/* Parse arguments */
	while ((c = getopt(argc, argv, "lad1htfSr")) != -1) {
		switch (c) {
		case 'l':
			psh_ls_common.mode = MODE_LONG;
			break;

		case 'a':
			psh_ls_common.all = 1;
			break;

		case '1':
			if (psh_ls_common.mode == MODE_NORMAL)
				psh_ls_common.mode = MODE_ONEPERLINE;
			break;

		case 't':
			psh_ls_common.cmp = psh_ls_cmpmtime;
			break;

		case 'f':
			psh_ls_common.cmp = NULL;
			break;

		case 'S':
			psh_ls_common.cmp = psh_ls_cmpsize;
			break;

		case 'r':
			psh_ls_common.reverse = -1;
			break;

		case 'd':
			psh_ls_common.dir = 1;
			break;

		case 'h':
		default:
			psh_ls_help();
			return EOK;
		}
	}

	/* Treat rest of arguments as paths */
	if (optind < argc) {
		paths = &argv[optind];
		npaths = argc - optind;
	}

	if (psh_ls_common.dir && (npaths == 0)) {
		currdir = ".";
		paths = &currdir;
		npaths++;
	}

	if ((npaths > 0) && ((psh_ls_common.odir = calloc(npaths, sizeof(int *))) == NULL)) {
		fprintf(stderr, "ls: out of memory\n");
		return -ENOMEM;
	}

	if ((ret = psh_ls_expandbuff(32)) < 0) {
		free(psh_ls_common.odir);
		return ret;
	}

	/* Try to stat all the given paths */
	for (i = 0; i < npaths; i++) {
		if ((ret = stat(paths[i], &psh_ls_common.files[nfiles].stat)) < 0) {
			fprintf(stderr, "ls: can't access %s: no such file or directory\n", paths[i]);
			continue;
		}

		if ((paths[i][strlen(paths[i]) - 1] == '/') && !S_ISDIR(psh_ls_common.files[nfiles].stat.st_mode)) {
			fprintf(stderr, "ls: can't access %s: not a directory\n", paths[i]);
			continue;
		}

		if (!S_ISDIR(psh_ls_common.files[nfiles].stat.st_mode) || psh_ls_common.dir) {
			if ((ret = psh_ls_readfile(&psh_ls_common.files[nfiles], paths[i])) < 0) {
				psh_ls_free();
				return ret;
			}
			nfiles++;
		}
		else {
			psh_ls_common.odir[i] = 1;
		}

		if (nfiles == psh_ls_common.fileinfosz) {
			if ((ret = psh_ls_expandbuff(psh_ls_common.fileinfosz * 2)) < 0) {
				psh_ls_free();
				return ret;
			}
		}
	}

	if (nfiles > 0) {
		if (psh_ls_common.cmp != NULL)
			qsort(psh_ls_common.files, nfiles, sizeof(fileinfo_t), psh_ls_common.cmp);

		if ((ret = psh_ls_printfiles(nfiles)) < 0) {
			psh_ls_free();
			return ret;
		}
	}

	i = 0;
	do {
		if (npaths == 0) {
			path = ".";
		}
		else if (psh_ls_common.odir[i]) {
			path = paths[i];
		}
		else {
			i++;
			continue;
		}

		if ((stream = opendir(path)) == NULL) {
			fprintf(stderr, "ls: failed to open directory %s\n", path);
			break;
		}

		/* Print dir name if there are more files/dirs */
		if (npaths > 1) {
			/* Print new line if there were entries already printed */
			if (nfiles > 0)
				putchar('\n');
			printf("%s:\n", path);
		}
		nfiles = 0;
		/* For each entry */
		while ((dir = readdir(stream)) != NULL) {
			if ((dir->d_name[0] == '.') && !psh_ls_common.all)
				continue;

			if ((ret = psh_ls_readentry(&psh_ls_common.files[nfiles], dir, path)) < 0) {
				closedir(stream);
				psh_ls_free();
				return ret;
			}

			nfiles++;
			if (nfiles == psh_ls_common.fileinfosz) {
				if ((ret = psh_ls_expandbuff(psh_ls_common.fileinfosz * 2)) < 0) {
					closedir(stream);
					psh_ls_free();
					return ret;
				}
			}
		}

		if (nfiles > 0) {
			if (psh_ls_common.cmp != NULL)
				qsort(psh_ls_common.files, nfiles, sizeof(fileinfo_t), psh_ls_common.cmp);
			psh_ls_printfiles(nfiles);
		}
		closedir(stream);
	} while (++i < npaths);

	psh_ls_free();

	return ret;
}


void __attribute__((constructor)) ls_registerapp(void)
{
	static psh_appentry_t app = {.name = "ls", .run = psh_ls, .info = psh_lsinfo};
	psh_registerapp(&app);
}
