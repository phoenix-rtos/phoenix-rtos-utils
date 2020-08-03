/*
 * Phoenix-RTOS
 *
 * Phoenix Shell ls
 *
 * Copyright 2020 Phoenix Systems
 * Author: Maciej Purski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

/* Some of the code is inspired by GNU ls implementation, thus we're retaining GNU copyright notice */
/* Copyright (C) 1985, 1988, 1990-1991, 1995-2010 Free Software Foundation,
 *  Inc.
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <termios.h>
#include <unistd.h>
#include <sys/minmax.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <time.h>
#include <pwd.h>
#include <grp.h>
#include "psh.h"


#define DIRCOLOR	"\033[34m" /* Blue */
#define EXECCOLOR	"\033[32m" /* Green */
#define SYMCOLOR	"\033[36m" /* Cyan */
#define DEVCOLOR	"\033[33;40m" /* Yellow with black bg */
#define FILECOLOR	"\033[37m"	  /* White */


/* Minimum column width for 1 char file name and 2 spaces */
#define MIN_COL_WIDTH 3
#define BUFFER_INIT_SIZE 32

struct fileinfo {
	char *name;
	size_t namelen;
	size_t memlen;
	struct stat stat;
	struct passwd *pw;
	struct group  *gr;
	uint32_t d_type;
};

struct colinfo
{
  int validlen;
  size_t linelen;
  size_t *colarr;
};

/* Maximum number of columns for display */
static size_t maxidx;

static struct colinfo *colinfo;

static size_t colinfoSize;

static struct fileinfo *files;

static size_t fileinfoSize;

struct winsize w;

static size_t ls_initColinfo(size_t nfiles)
{
	size_t maxcols = min(maxidx, nfiles);
	unsigned int i;
	size_t cols;

	/* Init cols */
	for (i = 0; i < maxcols; ++i) {
		size_t j;

		colinfo[i].validlen = 1;
		colinfo[i].linelen = (i + 1) * MIN_COL_WIDTH;
		for (j = 0; j <= i; ++j)
			colinfo[i].colarr[j] = MIN_COL_WIDTH;
	}

	/* Compute the maximum number of possible columns.  */
	size_t filesno;
	for (filesno = 0; filesno < nfiles; ++filesno) {
		struct fileinfo *f = &files[filesno];
		size_t name_length = f->namelen;
		size_t i;

		for (i = 0; i < maxcols; ++i) {
			if (colinfo[i].validlen) {
				size_t idx =  filesno / ((nfiles + i) / (i + 1));
				size_t real_length = name_length + (idx == i ? 0 : 2);
				if (colinfo[i].colarr[idx] < real_length) {
					colinfo[i].linelen += (real_length
					                       - colinfo[i].colarr[idx]);
					colinfo[i].colarr[idx] = real_length;
					colinfo[i].validlen = (colinfo[i].linelen
					                       < w.ws_col);
				}
			}
		}
	}

	/* Find maximum allowed columns.  */
	for (cols = maxcols; 1 < cols; --cols) {
		if (colinfo[cols - 1].validlen)
			break;
	}

	return cols;
}


static void ls_colorPrint(struct fileinfo *file, size_t width)
{
	char fmt[8];

	sprintf(fmt, "%%-%ds", width);
	switch (file->d_type) {
	case dtDir:
		printf(DIRCOLOR);
		break;
	case dtDev:
		printf(DEVCOLOR);
		break;
	case dtSymlink:
		printf(SYMCOLOR);
		break;
	case dtUnknown:
	case dtFile:
		printf(FILECOLOR);
		break;
	}
	printf(fmt, file->name);
	printf("\033[0m");
}


static int ls_cmpname(const void *t1, const void *t2)
{
	return strcasecmp(((struct fileinfo *)t1)->name, ((struct fileinfo *)t2)->name);
}


static int ls_readEntry(struct fileinfo *f, struct dirent *dir, const char *path, unsigned int full)
{
	char *name_re;
	char *fullname;
	size_t namelen = strlen(dir->d_name);
	size_t pathlen = strlen(path);

	if (f->memlen == 0) {
		f->memlen = namelen + 1;
		f->name = malloc(f->memlen);
		if (f->name == NULL) {
			printf("ls: out of memory\n");
			return -ENOMEM;
		}
	 } else if (f->memlen <= namelen) {
		f->memlen = namelen + 1;
		name_re = realloc(f->name, f->memlen);
		if (name_re == NULL) {
			printf("ls: out of memory\n");
			return -ENOMEM;
		}
		f->name = name_re;
	}

	strcpy(f->name, dir->d_name);
	f->namelen = namelen;

	if (full) {
		fullname = malloc((f->namelen + pathlen + 2) * sizeof(char));

		if (fullname == NULL) {
			printf("ls: out of memory\n");
			return -ENOMEM;
		}
		strcpy(fullname, path);

		if (fullname[pathlen - 1] != '/') {
			fullname[pathlen] = '/';
			strcpy(fullname + pathlen + 1, dir->d_name);
		} else {
			strcpy(fullname + pathlen, dir->d_name);
		}

		if (stat(fullname, &f->stat) != 0) {
			printf("ls: Can't stat file %s error: %d\n", dir->d_name);
			free(fullname);
			return -1;
		}

		free(fullname);

		f->pw = getpwuid(f->stat.st_uid);
		f->gr = getgrgid(f->stat.st_gid);
	}

	f->d_type = dir->d_type;

	return 0;
}

static unsigned int ls_numPlaces(unsigned int n)
{
	size_t r = 1;
	while (n > 9) {
		n /= 10;
		r++;
	}
	return r;
}

static void ls_printLong(size_t nfiles)
{
	unsigned int i;
	size_t linksz = 1;
	size_t usersz = 3;
	size_t grpsz = 3;
	size_t sizesz = 1;
	size_t daysz = 1;
	struct tm t;


	for (i = 0; i < nfiles; i++) {
		linksz = max(ls_numPlaces(files[i].stat.st_nlink), linksz);
		sizesz = max(ls_numPlaces(files[i].stat.st_size), sizesz);

		if (files[i].pw != NULL)
			usersz = max(strlen(files[i].pw->pw_name), usersz);

		if (files[i].gr != NULL)
			grpsz = max(strlen(files[i].gr->gr_name), grpsz);

		localtime_r(&files[i].stat.st_mtime, &t);
		if (t.tm_mday > 10)
			daysz = 2;
	}

	for (i = 0; i < nfiles; i++) {
		unsigned int j;
		char fmt[8];
		char perms[11];
		char buf[80];

		for (j = 0; j < 9; j++)
			perms[j] = '-';
		perms[10] = '\0';

		if (S_ISREG(files[i].stat.st_mode)) {
			perms[0] = '-';
		} else if (S_ISDIR(files[i].stat.st_mode)) {
			perms[0] = 'd';
		} else if (S_ISCHR(files[i].stat.st_mode)) {
			perms[0] = 'c';
		} else if (S_ISBLK(files[i].stat.st_mode)) {
			perms[0] = 'b';
		} else if (S_ISLNK(files[i].stat.st_mode)) {
			perms[0] = 'l';
		} else if (S_ISFIFO(files[i].stat.st_mode)) {
			perms[0] = 'p';
		} else if (S_ISFIFO(files[i].stat.st_mode)) {
			perms[0] = 's';
		}

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

		printf("%s ", perms);

		sprintf(fmt, "%%%dd ", linksz);
		printf(fmt, files[i].stat.st_nlink);

		sprintf(fmt, "%%-%ds ", usersz);
		if (files[i].pw)
			printf(fmt, files[i].pw->pw_name);
		else
			printf(fmt, "---");

		sprintf(fmt, "%%-%ds ", grpsz);
		files[i].gr = getgrgid(files[i].stat.st_gid);
		if (files[i].gr)
			printf(fmt, files[i].gr->gr_name);
		else
			printf(fmt, "---");

		sprintf(fmt, "%%%dd ", sizesz);
		printf(fmt, files[i].stat.st_size);

		localtime_r(&files[i].stat.st_mtime, &t);

		strftime(buf, 80, "%b ", &t);
		sprintf(fmt, "%%%dd ", daysz);
		sprintf(buf + 4, fmt, t.tm_mday);
		strftime(buf + 5 + daysz, 75 - daysz, "%H:%M", &t);
		printf("%s ", buf);

		ls_colorPrint(&files[i], files[i].namelen);
		putchar('\n');
	}
}

static void ls_printMultiline(size_t nfiles)
{
	size_t ncols = ls_initColinfo(nfiles);
	size_t nrows = nfiles / ncols + (nfiles % ncols != 0);
	struct colinfo *line_fmt = &colinfo[ncols - 1];
	unsigned int row;

	for (row = 0; row < nrows; row++) {
		unsigned int col;
		for (col = 0; col < ncols; col++) {
			unsigned int idx = col * nrows + row;

			if (idx >= nfiles)
				continue;

			ls_colorPrint(&files[idx], max(files[idx].namelen, min(line_fmt->colarr[col], w.ws_col)));
		}
		putchar('\n');
	}
}

static void ls_printFiles(size_t nfiles, int oneline, int full)
{
	if (oneline) {
		unsigned int i;
		if (full) {
			ls_printLong(nfiles);
		} else {
			for (i = 0; i < nfiles; i++) {
				ls_colorPrint(&files[i], files[i].namelen);
				putchar('\n');
			}
		}
	} else {
		ls_printMultiline(nfiles);
	}
}

static int ls_buffersInit(size_t sz)
{
	unsigned int i;
	unsigned int maxcols = min(sz, maxidx);

	files = malloc(sz * sizeof(struct fileinfo));
	if (files == NULL) {
		printf("ls: out of memory\n");
		return -ENOMEM;
	}

	for (i = 0; i < sz; i++) {
		files[i].memlen = 0;
		files[i].name = NULL;
	}
	fileinfoSize = sz;

	colinfo = malloc(maxcols * sizeof(struct colinfo));
	if (colinfo == NULL) {
		printf("ls: out of memory\n");
		return -ENOMEM;
	}

	for (i = 0; i < maxcols; i++)
		 colinfo[i].colarr = NULL;

	for (i = 0; i < maxcols; i++) {
		colinfo[i].colarr = malloc((i + 1) * sizeof(size_t));

		if (colinfo[i].colarr == NULL) {
			printf("ls: out of memory\n");
			return -ENOMEM;
		}
	}
	colinfoSize = maxcols;

	return 0;
}

static int ls_bufferExpand(size_t sz)
{
	unsigned int i;
	unsigned int maxcols = min(sz, maxidx);
	void *reptr;

	reptr = realloc(files, sz * sizeof(struct fileinfo));
	if (reptr == NULL) {
		printf("ls: out of memory\n");
		return -ENOMEM;
	}

	files = reptr;
	for (i = fileinfoSize; i < sz; i++) {
		files[i].memlen = 0;
		files[i].name = NULL;
	}
	fileinfoSize = sz;

	if (maxcols > colinfoSize) {
		reptr = realloc(colinfo, maxcols * sizeof(struct colinfo));
		if (reptr == NULL) {
			printf("ls: out of memory\n");
			return -ENOMEM;
		}
		colinfo = reptr;

		for (i = colinfoSize; i < maxcols; i++)
			colinfo[i].colarr = NULL;

		for (i = colinfoSize; i < maxcols; i++) {
			colinfo[i].colarr = malloc((i + 1) * sizeof(size_t));

			if (colinfo[i].colarr == NULL) {
				printf("ls: out of memory\n");
				return -ENOMEM;
			}
		}
		colinfoSize = maxcols;
	}

	return 0;
}


int psh_ls(char *args)
{
	char **paths = NULL;
	unsigned int npaths = 0;
	char **reptr;
	unsigned int len;
	char cwd[] = ".";
	int ret = 0;
	int all = 0;
	int full = 0;
	int line = 0;
	int nfiles = 0;
	unsigned int i;


	/* In case of ioctl fail set default window size */
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w)) {
		w.ws_col = 80;
		w.ws_row = 25;
	}

	maxidx = max(1, w.ws_col / MIN_COL_WIDTH);

	/* Parse arguments */
	while ((args = psh_nextString(args, &len)) && len) {
		if (args[0] != '-') {
			if (paths) {
				reptr = realloc(paths, (npaths + 1) * sizeof(char *));
				if (reptr == NULL) {
					printf("ls: out of memory\n");
					ret = -ENOMEM;
					goto freePaths;
				}
				paths = reptr;
			} else {
				paths = malloc(sizeof(char *));
				if (paths == NULL) {
					printf("ls: out of memory\n");
					return -ENOMEM;
				}
			}

			if ((paths[npaths] = strdup(args)) == NULL) {
				printf("ls: out of memory\n");
				ret = -ENOMEM;
				goto freePaths;
			}
			npaths++;
		} else if (!strcmp(args, "-a") || strchr(args, 'a')) {
			all = 1;
		} else if (!strcmp(args, "-l") || strchr(args, 'l')) {
			full = 1;
			line = 1;
		} else if (!strcmp(args, "-1") || strchr(args, '1')) {
			line = 1;
		} else {
			printf("ls: unknown option '%s'\n", args);
			ret = EOK;
			goto freePaths;
		}

		args += len + 1;
	}

	if ((ret = ls_buffersInit(BUFFER_INIT_SIZE)) != 0)
		goto freePaths;

	i = 0;
	do {
		DIR *stream;
		struct dirent *dir;
		char *path;
		if (npaths == 0)
			path = cwd;
		else
			path = paths[i];

		stream = opendir(path);
		if (stream == NULL) {
			printf("%s: no such directory\n", path);
			break;
		}

		if (npaths > 1)
			printf("%s:\n", path);
		nfiles = 0;
		/* For each entry */
		while ((dir = readdir(stream)) != NULL) {
			if (dir->d_name[0] == '.' && !all)
				continue;

			if ((ret = ls_readEntry(&files[nfiles], dir, path, full)) != 0)
				goto freePaths;

			nfiles++;

			if (nfiles == fileinfoSize) {
				if ((ret = ls_bufferExpand(fileinfoSize * 2)) != 0)
					goto freePaths;
			}
		}
		if (nfiles > 0) {
			qsort(files, nfiles, sizeof(struct fileinfo), ls_cmpname);
			ls_printFiles(nfiles, line, full);
		}
		closedir(stream);

		i++;
		if (npaths > 1 && i < npaths)
			printf("\n");
	} while (i < npaths);

freePaths:
	for (i = 0; i < npaths; i++)
		free(paths[i]);
	if (npaths > 0)
		free(paths);

	if (files != NULL) {
		for (i = 0; i < fileinfoSize; i++)
			free(files[i].name);

		free(files);
	}

	if (colinfo != NULL) {
		for (i = 0; i < colinfoSize; i++)
			free(colinfo[i].colarr);

		free(colinfo);
	}

	return ret;
}
