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


#define EXEC_MASK (S_IXUSR | S_IXGRP | S_IXOTH)

#define BUFFER_INIT_SIZE 32

static const char cwd[] = ".";

struct fileinfo {
	char *name;
	size_t namelen;
	size_t memlen;
	struct stat stat;
	struct passwd *pw;
	struct group  *gr;
	uint32_t d_type;
};


static struct fileinfo *files;

static size_t fileinfoSize;

static struct winsize w;


static void ls_printHelp(void)
{
	static const char help[] = "Command line arguments:\n"
				   "  -h:  prints help\n"
				   "  -l:  long listing format\n"
				   "  -1:  one entry per line\n"
				   "  -a:  do not ignore entries starting with .\n\n";

	printf(help);
}


static size_t *ls_computeRows(size_t *nrowsres, size_t *ncolsres, size_t nfiles)
{
	unsigned int col;
	unsigned int i;
	size_t nrows = 1;
	size_t ncols = nfiles;
	size_t *colsz;
	size_t sum = 0;

	/* Estimate lower bound of nrows */
	for (i = 0; i < nfiles; i++)
		sum += files[i].namelen;

	nrows = sum / w.ws_col + 1;
	ncols = nfiles / nrows + 1;

	colsz = malloc(ncols * sizeof(size_t));
	if (colsz == NULL) {
		printf("ls: out of memory\n");
		return NULL;
	}

	while (nrows <= nfiles) {
		if (nfiles % nrows == 0)
			ncols = nfiles / nrows;
		else
			ncols = nfiles / nrows + 1;

		for (i = 0; i < ncols; i++)
			colsz[i] = 0;

		/* Compute widths of each column */
		for (i = 0; i < nfiles; i++) {
			col = i / nrows;
			colsz[col] = max(colsz[col], files[i].namelen + 2);
		}
		colsz[ncols - 1] -= 2;

		sum = 0;
		/* Compute all columns but last */
		for (col = 0; col < ncols; col++)
			sum += colsz[col];

		if (sum < w.ws_col)
			break;
		nrows++;
	}

	*nrowsres = nrows;
	*ncolsres = ncols;

	return colsz;
}


static void ls_colorPrint(struct fileinfo *file, size_t width)
{
	char fmt[16];
	const char *p = "";

	sprintf(fmt, "%%s%%-%ds%%s", width);
	if (S_ISREG(file->stat.st_mode) && file->stat.st_mode & EXEC_MASK) {
		p = EXECCOLOR;
	} else if (S_ISDIR(file->stat.st_mode)) {
		p = DIRCOLOR;
	} else if (S_ISCHR(file->stat.st_mode) || S_ISBLK(file->stat.st_mode)) {
		p = DEVCOLOR;
	} else if (S_ISLNK(file->stat.st_mode)) {
		p = SYMCOLOR;
	}
	printf(fmt, p, file->name, "\033[0m");
}


static int ls_cmpname(const void *t1, const void *t2)
{
	return strcasecmp(((struct fileinfo *)t1)->name, ((struct fileinfo *)t2)->name);
}


static int ls_namecpy(struct fileinfo *f, const char *name)
{
	size_t namelen = strlen(name);
	char *name_re;


	if (f->memlen <= namelen) {
		name_re = realloc(f->name, namelen + 1);
		if (name_re == NULL) {
			printf("ls: out of memory\n");
			return -ENOMEM;
		}
		f->name = name_re;
		f->namelen = namelen;
		f->memlen = namelen + 1;
	}

	strcpy(f->name, name);

	return 0;
}


static int ls_readEntry(struct fileinfo *f, struct dirent *dir, const char *path, unsigned int full)
{
	char *fullname;
	size_t pathlen = strlen(path);
	int ret;

	if ((ret = ls_namecpy(f, dir->d_name)) != 0)
		return ret;

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

	if ((ret = lstat(fullname, &f->stat)) != 0) {
		printf("ls: Can't stat file %s.\n", dir->d_name);
		free(fullname);
		return ret;
	}

	if (full) {
		f->pw = getpwuid(f->stat.st_uid);
		f->gr = getgrgid(f->stat.st_gid);
	}

	f->d_type = dir->d_type;
	free(fullname);

	return 0;
}


static int ls_readFile(struct fileinfo *f, char *path, int full)
{
	int ret;

	if ((ret = ls_namecpy(f, path)) != 0)
		return ret;


	if ((ret = lstat(path, &f->stat)) != 0) {
		printf("ls: Can't stat file %s.\n", path);
		return ret;
	}

	if (full) {
		f->pw = getpwuid(f->stat.st_uid);
		f->gr = getgrgid(f->stat.st_gid);
	}

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
	unsigned int i, j;
	size_t linksz = 1;
	size_t usersz = 3;
	size_t grpsz = 3;
	size_t sizesz = 1;
	size_t daysz = 1;
	struct tm t;
	char fmt[8];
	char perms[11];
	char buf[80];


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
		for (j = 0; j < 10; j++)
			perms[j] = '-';
		perms[10] = '\0';

		if (S_ISDIR(files[i].stat.st_mode)) {
			perms[0] = 'd';
		} else if (S_ISCHR(files[i].stat.st_mode)) {
			perms[0] = 'c';
		} else if (S_ISBLK(files[i].stat.st_mode)) {
			perms[0] = 'b';
		} else if (S_ISLNK(files[i].stat.st_mode)) {
			perms[0] = 'l';
		} else if (S_ISFIFO(files[i].stat.st_mode)) {
			perms[0] = 'p';
		} else if (S_ISSOCK(files[i].stat.st_mode)) {
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


static int ls_printMultiline(size_t nfiles)
{
	size_t ncols, nrows;
	size_t *colsz;
	unsigned int row, col, idx;

	colsz = ls_computeRows(&nrows, &ncols, nfiles);
	if (colsz == NULL)
		return -ENOMEM;

	for (row = 0; row < nrows; row++) {
		for (col = 0; col < ncols; col++) {
			idx = col * nrows + row;

			if (idx >= nfiles)
				continue;

			ls_colorPrint(&files[idx], max(files[idx].namelen, min(colsz[col], w.ws_col)));
		}
		putchar('\n');
	}
	free(colsz);

	return 0;
}


static int ls_printFiles(size_t nfiles, int oneline, int full)
{
	unsigned int i;
	int ret = 0;

	if (full) {
		ls_printLong(nfiles);
	} else if (oneline) {
		for (i = 0; i < nfiles; i++) {
			ls_colorPrint(&files[i], files[i].namelen);
			putchar('\n');
		}
	} else {
		ret = ls_printMultiline(nfiles);
	}

	return ret;
}


static int ls_buffersInit(size_t sz)
{
	unsigned int i;

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

	return 0;
}


static int ls_bufferExpand(size_t sz)
{
	unsigned int i;
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

	return 0;
}


static void ls_free(char **paths, size_t npaths)
{
	unsigned int i;

	if (paths != NULL) {
		for (i = 0; i < npaths; i++)
			free(paths[i]);

		free(paths);
	}

	if (files != NULL) {
		for (i = 0; i < fileinfoSize; i++)
			free(files[i].name);

		free(files);
	}
}


int psh_ls(char *args)
{
	char **paths = NULL;
	unsigned int npaths = 0;
	char **reptr;
	unsigned int len;
	int ret = 0, all = 0, full = 0, line = 0;
	int nfiles = 0;
	unsigned int i;
	struct stat s;
	DIR *stream;
	struct dirent *dir;
	const char *path;


	/* In case of ioctl fail set default window size */
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w)) {
		w.ws_col = 80;
		w.ws_row = 25;
	}

	fileinfoSize = 0;
	files = NULL;

	/* Parse arguments */
	while ((args = psh_nextString(args, &len)) && len) {
		if (args[0] != '-') {
			reptr = realloc(paths, (npaths + 1) * sizeof(char *));
			if (reptr == NULL) {
				printf("ls: out of memory\n");
				ls_free(paths, npaths);
				return -ENOMEM;
			}
			paths = reptr;

			if ((paths[npaths] = strdup(args)) == NULL) {
				printf("ls: out of memory\n");
				ls_free(paths, npaths);
				return -ENOMEM;
			}
			npaths++;
		} else if (!strcmp(args, "-a")) {
			all = 1;
		} else if (!strcmp(args, "-l")) {
			full = 1;
			line = 1;
		} else if (!strcmp(args, "-1")) {
			line = 1;
		} else if (!strcmp(args, "-h")) {
			ls_printHelp();
			ls_free(paths, npaths);
			return EOK;
		} else {
			printf("ls: unknown option '%s'\n", args);
			ls_free(paths, npaths);
			return -1;
		}

		args += len + 1;
	}

	if ((ret = ls_buffersInit(BUFFER_INIT_SIZE)) != 0) {
		ls_free(paths, npaths);
		return ret;
	}

	/* First collect non-dir files and set to null after handling */
	nfiles = 0;
	for (int i = 0; i < npaths; i++) {
		if (stat(paths[i], &s) != 0) {
			printf("ls: cannot access %s: No such file or directory\n", paths[i]);
			free(paths[i]);
			paths[i] = NULL;
		} else if (!S_ISDIR(s.st_mode)) {
			if ((ret = ls_readFile(&files[nfiles], paths[i], full)) != 0) {
				ls_free(paths, npaths);
				return ret;
			}

			nfiles++;
			free(paths[i]);
			paths[i] = NULL;
		}
	}

	if (nfiles > 0) {
		qsort(files, nfiles, sizeof(struct fileinfo), ls_cmpname);
		ret = ls_printFiles(nfiles, line, full);
		if (ret != 0) {
			ls_free(paths, npaths);
			return ret;
		}
	}

	i = 0;
	do {
		if (npaths == 0)
			path = cwd;
		else
			path = paths[i];

		if (path == NULL) {
			i++;
			continue;
		}

		stream = opendir(path);
		if (stream == NULL) {
			printf("%s: no such directory\n", path);
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
			if (dir->d_name[0] == '.' && !all)
				continue;

			if ((ret = ls_readEntry(&files[nfiles], dir, path, full)) != 0) {
				closedir(stream);
				ls_free(paths, npaths);
				return ret;
			}

			nfiles++;
			if (nfiles == fileinfoSize) {
				if ((ret = ls_bufferExpand(fileinfoSize * 2)) != 0) {
					closedir(stream);
					ls_free(paths, npaths);
					return ret;
				}
			}
		}

		if (nfiles > 0) {
			qsort(files, nfiles, sizeof(struct fileinfo), ls_cmpname);
			ls_printFiles(nfiles, line, full);
		}
		closedir(stream);

		i++;
	} while (i < npaths);

	ls_free(paths, npaths);

	return ret;
}
