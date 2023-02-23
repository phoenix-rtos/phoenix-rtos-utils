/*
 * Phoenix-RTOS
 *
 * Phoenix-RTOS SHell
 *
 * pshapp - interactive Phoenix SHell
 *
 * Copyright 2017, 2018, 2020, 2021 Phoenix Systems
 * Author: Pawel Pisarczyk, Jan Sikorski, Lukasz Kosinski, Mateusz Niewiadomski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <paths.h>
#include <pwd.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/pwman.h>
#include <sys/threads.h>

#include <posix/utils.h>

#include "../psh.h"


/* Shell definitions */
#define PROMPT       "(psh)% " /* Shell prompt */
#define SCRIPT_MAGIC ":{}:"    /* Every psh script should start with this line */
#define CMDSZ        128       /* Command buffer size */
#define HISTSZ       512       /* Command history size */


/* Misc definitions */
#define BP_OFFS     0  /* Offset of 0 exponent entry in binary prefix table */
#define BP_EXP_OFFS 10 /* Offset between consecutive entries exponents in binary prefix table */
#define SI_OFFS     8  /* Offset of 0 exponent entry in SI prefix table */
#define SI_EXP_OFFS 3  /* Offset between consecutive entries exponents in SI prefix table */


/* Special key codes */
enum { kUp = 1, kDown, kRight, kLeft, kDelete, kHome, kEnd };


typedef struct {
	int n;     /* Command length (each word is followed by '\0') */
	char *cmd; /* Command pointer */
} psh_histent_t;


typedef struct {
	int hb;                        /* History begin index (oldest command) */
	int he;                        /* History end index (newest command) */
	psh_histent_t entries[HISTSZ]; /* Command history entries */
} psh_hist_t;


typedef struct {
	psh_hist_t *cmdhist;
	unsigned char newline;
} pshapp_common_t;


/* Binary (base 2) prefixes */
static const char *bp[] = {
	"",  /* 2^0         */
	"K", /* 2^10   kibi */
	"M", /* 2^20   mebi */
	"G", /* 2^30   gibi */
	"T", /* 2^40   tebi */
	"P", /* 2^50   pebi */
	"E", /* 2^60   exbi */
	"Z", /* 2^70   zebi */
	"Y"  /* 2^80   yobi */
};


/* SI (base 10) prefixes */
static const char *si[] = {
	"y", /* 10^-24 yocto */
	"z", /* 10^-21 zepto */
	"a", /* 10^-18 atto  */
	"f", /* 10^-15 femto */
	"p", /* 10^-12 pico  */
	"n", /* 10^-9  nano  */
	"u", /* 10^-6  micro */
	"m", /* 10^-3  milli */
	"",  /* 10^0         */
	"k", /* 10^3   kilo  */
	"M", /* 10^6   mega  */
	"G", /* 10^9   giga  */
	"T", /* 10^12  tera  */
	"P", /* 10^15  peta  */
	"E", /* 10^18  exa   */
	"Z", /* 10^21  zetta */
	"Y", /* 10^24  yotta */
};


pshapp_common_t pshapp_common;


void _psh_exit(int code)
{
	keepidle(0);
	_exit(code);
}


static int psh_mod(int x, int y)
{
	int ret = x % y;

	if (ret < 0)
		ret += abs(y);

	return ret;
}


static int psh_div(int x, int y)
{
	return (x - psh_mod(x, y)) / y;
}


static int psh_log(unsigned int base, unsigned int x)
{
	int ret = 0;

	while (x /= base)
		ret++;

	return ret;
}


static int psh_pow(int x, unsigned int y)
{
	int ret = 1;

	while (y) {
		if (y & 1)
			ret *= x;
		y >>= 1;
		if (!y)
			break;
		x *= x;
	}

	return ret;
}


static const char *psh_bp(int exp)
{
	exp = psh_div(exp, BP_EXP_OFFS) + BP_OFFS;

	if ((exp < 0) || (exp >= sizeof(bp) / sizeof(bp[0])))
		return NULL;

	return bp[exp];
}


static const char *psh_si(int exp)
{
	exp = psh_div(exp, SI_EXP_OFFS) + SI_OFFS;

	if ((exp < 0) || (exp >= sizeof(si) / sizeof(si[0])))
		return NULL;

	return si[exp];
}


int psh_prefix(unsigned int base, int x, int y, unsigned int prec, char *buff)
{
	int div = psh_log(base, abs(x)), exp = div + y;
	int offs, ipart, fpart;
	const char *(*fp)(int);
	const char *prefix;

	/* Support precision for up to 8 decimal places */
	if (prec > 8)
		return -EINVAL;

	switch (base) {
		/* Binary prefix */
		case 2:
			fp = psh_bp;
			offs = BP_EXP_OFFS;
			break;

		/* SI prefix */
		case 10:
			fp = psh_si;
			offs = SI_EXP_OFFS;
			break;

		default:
			return -EINVAL;
	}

	/* div < 0 => accumulate extra exponents in x */
	if ((div -= psh_mod(exp, offs)) < 0) {
		x *= psh_pow(base, -div);
		div = 0;
	}
	div = psh_pow(base, div);

	/* Save integer part and fractional part as percentage */
	ipart = abs(x) / div;
	fpart = (int)((uint64_t)psh_pow(10, prec + 1) * (abs(x) % div) / div);

	/* Round the result */
	if ((fpart = (fpart + 5) / 10) == psh_pow(10, prec)) {
		ipart++;
		fpart = 0;
		if (ipart == psh_pow(base, offs)) {
			ipart = 1;
			exp += offs;
		}
	}

	/* Remove trailing zeros */
	while (fpart && !(fpart % 10)) {
		fpart /= 10;
		prec--;
	}

	/* Get the prefix */
	if ((prefix = fp((!ipart && !fpart) ? y : exp)) == NULL)
		return -EINVAL;

	if (x < 0)
		*buff++ = '-';

	if (fpart)
		sprintf(buff, "%d.%0*d%s", ipart, prec, fpart, prefix);
	else
		sprintf(buff, "%d%s", ipart, prefix);

	return 0;
}


static void psh_histentcmd(char **cmd, psh_histent_t *entry)
{
	int i;

	for (i = 0; i < entry->n; i++)
		(*cmd)[i] = (entry->cmd[i] == '\0') ? ' ' : entry->cmd[i];
}


static void psh_printhistent(psh_histent_t *entry)
{
	int i;

	for (i = 0; i < entry->n; i++)
		write(STDOUT_FILENO, (entry->cmd[i] == '\0') ? " " : entry->cmd + i, 1);
}


static int psh_completepath(char *dir, char *base, char ***files)
{
	size_t i, size = 32, dlen = strlen(dir), blen = strlen(base);
	int nfiles = 0, err = 0;
	char *path, **rfiles;
	struct stat st;
	DIR *stream;

	*files = NULL;

	do {
		if ((stream = opendir(dir)) == NULL)
			break;

		if (dir[dlen - 1] != '/')
			dir[dlen++] = '/';

		if ((*files = malloc(size * sizeof(char *))) == NULL) {
			fprintf(stderr, "\r\npsh: out of memory\r\n");
			err = -ENOMEM;
			break;
		}

		while (readdir(stream) != NULL) {
			if ((stream->dirent->d_namlen < blen) || strncmp(stream->dirent->d_name, base, blen))
				continue;

			if (!blen && (!strcmp(stream->dirent->d_name, ".") || !strcmp(stream->dirent->d_name, "..")))
				continue;

			i = dlen + stream->dirent->d_namlen;
			if ((path = malloc(i + 1)) == NULL) {
				fprintf(stderr, "\r\npsh: out of memory\r\n");
				err = -ENOMEM;
				break;
			}
			memcpy(path, dir, dlen);
			strcpy(path + dlen, stream->dirent->d_name);

			if ((err = stat(path, &st)) < 0) {
				fprintf(stderr, "\r\npsh: can't stat %s\r\n", path);
				free(path);
				break;
			}
			free(path);

			if (nfiles == size) {
				if ((rfiles = realloc(*files, 2 * size * sizeof(char *))) == NULL) {
					fprintf(stderr, "\r\npsh: out of memory\r\n");
					err = -ENOMEM;
					break;
				}
				*files = rfiles;
				size *= 2;
			}

			if (((*files)[nfiles] = malloc(stream->dirent->d_namlen + 2)) == NULL) {
				fprintf(stderr, "\r\npsh: out of memory\r\n");
				err = -ENOMEM;
				break;
			}
			memcpy((*files)[nfiles], stream->dirent->d_name, stream->dirent->d_namlen);
			(*files)[nfiles][stream->dirent->d_namlen] = (S_ISDIR(st.st_mode)) ? '/' : ' ';
			(*files)[nfiles][stream->dirent->d_namlen + 1] = '\0';
			nfiles++;
		}
	} while (0);

	if (err < 0) {
		for (i = 0; i < nfiles; i++)
			free((*files)[i]);
		free(*files);
		*files = NULL;
		return err;
	}

	return nfiles;
}


static int psh_printfiles(char **files, int nfiles)
{
	int i, row, col, rows, cols, *colsz, len = 0;
	struct winsize ws;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) < 0) {
		ws.ws_row = 25;
		ws.ws_col = 80;
	}

	for (i = 0; i < nfiles; i++)
		len += strlen(files[i]);

	rows = len / ws.ws_col + 1;
	cols = nfiles / rows + 1;

	if ((colsz = malloc(cols * sizeof(int))) == NULL) {
		fprintf(stderr, "\r\npsh: out of memory\r\n");
		return -ENOMEM;
	}

	for (; rows <= nfiles; rows++) {
		cols = nfiles / rows + !!(nfiles % rows);

		for (i = 0; i < cols; i++)
			colsz[i] = 0;

		for (i = 0; i < nfiles; i++) {
			col = i / rows;
			if ((len = strlen(files[i])) + 2 > colsz[col])
				colsz[col] = len + 2;
		}
		colsz[cols - 1] -= 2;

		for (len = 0, col = 0; col < cols; col++)
			len += colsz[col];

		if (len < ws.ws_col)
			break;
	}

	printf("\r\n");
	for (row = 0; row < rows; row++) {
		for (col = 0; col < cols; col++) {
			if ((i = col * rows + row) >= nfiles)
				continue;
			if ((len = colsz[col]) > ws.ws_col)
				len = ws.ws_col;
			printf("%-*s", len, files[i]);
		}
		printf("\r\n");
	}

	fflush(stdout);
	free(colsz);

	return 0;
}


static void psh_movecursor(int col, int n)
{
	struct winsize ws;
	int p;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) < 0) {
		ws.ws_row = 25;
		ws.ws_col = 80;
	}
	col %= ws.ws_col;

	if (col + n < 0) {
		p = (-(col + n) + ws.ws_col - 1) / ws.ws_col;
		n += p * ws.ws_col;
		printf("\033[%dA", p);
	}
	else if (col + n > ws.ws_col - 1) {
		p = (col + n) / ws.ws_col;
		n -= p * ws.ws_col;
		printf("\033[%dB", p);
	}

	if (n > 0)
		printf("\033[%dC", n);
	else if (n < 0)
		printf("\033[%dD", -n);

	fflush(stdout);
}


static int psh_cmpname(const void *n1, const void *n2)
{
	return strcasecmp(*(char **)n1, *(char **)n2);
}


static int psh_keyCode(const char *buff, int *pEsc)
{
	int n, esc = *pEsc;
	static const struct {
		const char *escSeq;
		int keyCode;
	} keys[] = {
		{ "A", kUp },
		{ "B", kDown },
		{ "C", kRight },
		{ "D", kLeft },
		{ "F", kEnd },
		{ "H", kHome },
		{ "1~", kHome },
		{ "3~", kDelete },
		{ "4~", kEnd },
		{ "7~", kHome },
		{ "8~", kEnd }
	};

	if (esc < 4)
		return -EAGAIN;

	if (buff[0] == '^' && buff[1] == '[' && (buff[2] == '[' || buff[2] == 'O')) {
		buff += 3;
		esc -= 3;

		for (n = 0; n < sizeof(keys) / sizeof(keys[0]); ++n) {
			if (strncmp(buff, keys[n].escSeq, esc) != 0)
				continue;

			if (esc != strlen(keys[n].escSeq))
				return -EAGAIN;

			*pEsc = 0;
			return keys[n].keyCode;
		}
	}

	return -EINVAL;
}


extern void cfmakeraw(struct termios *termios);


static int psh_readcmd(struct termios *orig, psh_hist_t *cmdhist, char **cmd)
{
	int i, k, nfiles, err = 0, esc = 0, n = 0, m = 0, ln = 0, hp = cmdhist->he;
	char c, *path, *fpath, *dir, *base, **files, buff[8];
	struct termios raw = *orig;

	/* Allocate command buffer (one extra character for '\0') */
	if ((*cmd = malloc(CMDSZ + 1)) == NULL) {
		fprintf(stderr, "\npsh: out of memory\n");
		return -ENOMEM;
	}

	/* Enable raw mode for command processing */
	cfmakeraw(&raw);

	if ((err = tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw)) < 0) {
		fprintf(stderr, "\npsh: failed to enable raw mode\n");
		free(*cmd);
		return err;
	}

	for (;;) {
		read(STDIN_FILENO, &c, 1);

		/* Process control characters */
		if ((c < 0x20) || (c == 0x7f)) {
			/* Cancel escape code processing */
			esc = 0;

			/* ETX => cancel command */
			if (c == '\003') {
				printf("^C");
				if (m > 2)
					psh_movecursor(n + sizeof(PROMPT) + 1, m - 2);
				printf("\r\n");
				n = m = 0;
				break;
			}
			/* EOT => delete next character/exit */
			else if (c == '\004') {
				if (m) {
					if (hp != cmdhist->he) {
						psh_histentcmd(cmd, cmdhist->entries + hp);
						hp = cmdhist->he;
					}
					memmove(*cmd + n, *cmd + n + 1, --m);
					write(STDOUT_FILENO, "\033[0J", 4);
					write(STDOUT_FILENO, *cmd + n, m);
					psh_movecursor(n + m + sizeof(PROMPT) - 1, -m);
				}
				else if (!(n + m)) {
					printf("exit\r\n");
					free(*cmd);
					*cmd = NULL;
					break;
				}
			}
			/* BS => remove last character */
			else if ((c == '\b') || (c == '\177')) {
				if (n) {
					if (hp != cmdhist->he) {
						psh_histentcmd(cmd, cmdhist->entries + hp);
						hp = cmdhist->he;
					}
					write(STDOUT_FILENO, "\b", 1);
					n--;
					memmove(*cmd + n, *cmd + n + 1, m);
					write(STDOUT_FILENO, "\033[0J", 4);
					write(STDOUT_FILENO, *cmd + n, m);
					psh_movecursor(n + m + sizeof(PROMPT) - 1, -m);
				}
			}
			/* TAB => autocomplete paths */
			else if (c == '\t') {
				path = (hp != cmdhist->he) ? cmdhist->entries[hp].cmd : *cmd;
				for (i = n; i && (path[i - 1] != ' ') && (path[i - 1] != '\0'); i--)
					;

				/* Skip empty path */
				if (i == n)
					continue;

				path += i;
				i = n - i;
				c = path[i];
				path[i] = '\0';
				/* allow_missing_leaf == 1 -> partial file names */
				if ((fpath = resolve_path(path, NULL, 1, 1)) == NULL) {
					path[i] = c;
					if (errno == ENOENT)
						continue;
					fprintf(stderr, "\r\npsh: resolve_path() failed\r\n");
					err = -errno;
					break;
				}
				path[i] = c;

				nfiles = strlen(fpath);
				if ((path[i - 1] == '/') && (fpath[nfiles - 1] != '/')) {
					fpath[nfiles] = '/';
					fpath[nfiles + 1] = '\0';
				}
				splitname(fpath, &base, &dir);

				do {
					if ((nfiles = psh_completepath(dir, base, &files)) <= 0) {
						err = nfiles;
						break;
					}
					/* Print hints */
					if (nfiles > 1) {
						psh_movecursor(n + sizeof(PROMPT) - 1, m);
						qsort(files, nfiles, sizeof(char *), psh_cmpname);
						if ((err = psh_printfiles(files, nfiles)) < 0)
							break;
						write(STDOUT_FILENO, "\033[0J", 4);
						write(STDOUT_FILENO, PROMPT, sizeof(PROMPT) - 1);
						if (hp == cmdhist->he)
							write(STDOUT_FILENO, *cmd, n + m);
						else
							psh_printhistent(cmdhist->entries + hp);
						psh_movecursor(n + m + sizeof(PROMPT) - 1, -m);
					}
					/* Complete path */
					else {
						if (hp != cmdhist->he) {
							psh_histentcmd(cmd, cmdhist->entries + hp);
							hp = cmdhist->he;
						}
						i = strlen(files[0]) - strlen(base);
						if (n + m + i > CMDSZ)
							i = CMDSZ - n - m;
						memmove(*cmd + n + i, *cmd + n, m);
						memcpy(*cmd + n, files[0] + strlen(base), i);
						write(STDOUT_FILENO, *cmd + n, i + m);
						n += i;
						psh_movecursor(n + m + sizeof(PROMPT) - 1, -m);
					}
				} while (0);

				for (i = 0; i < nfiles; i++)
					free(files[i]);
				free(files);
				free(fpath);

				if (err < 0)
					break;
			}
			/* FF => clear screen */
			else if (c == '\014') {
				write(STDOUT_FILENO, "\033[f", 3);
				write(STDOUT_FILENO, "\033[0J", 4);
				write(STDOUT_FILENO, PROMPT, sizeof(PROMPT) - 1);
				if (hp != cmdhist->he)
					psh_printhistent(cmdhist->entries + hp);
				else
					write(STDOUT_FILENO, *cmd, n + m);
				psh_movecursor(n + m + sizeof(PROMPT) - 1, -m);
			}
			/* LF or CR => go to new line and break (finished reading command) */
			else if ((c == '\r') || (c == '\n')) {
				/* handle crlf line ending */
				if (c == '\r') {
					pshapp_common.newline = 1;
				}
				/* lf after cr - skip */
				else if ((c == '\n') && (pshapp_common.newline != 0)) {
					pshapp_common.newline = 0;
					break;
				}

				if (hp != cmdhist->he) {
					psh_histentcmd(cmd, cmdhist->entries + hp);
					hp = cmdhist->he;
				}
				psh_movecursor(n + sizeof(PROMPT) - 1, m);
				write(STDOUT_FILENO, "\r\n", 2);
				break;
			}
			else {
				pshapp_common.newline = 0;
				/* ESC => process escape code keys */
				if (c == '\033') {
					buff[esc++] = '^';
					buff[esc++] = '[';
				}
			}
		}
		/* Process regular characters */
		else {
			if (!esc) {
				if (n + m < CMDSZ) {
					if (hp != cmdhist->he) {
						psh_histentcmd(cmd, cmdhist->entries + hp);
						hp = cmdhist->he;
					}
					memmove(*cmd + n + 1, *cmd + n, m);
					(*cmd)[n++] = c;
					write(STDOUT_FILENO, *cmd + n - 1, m + 1);
					psh_movecursor(n + m + sizeof(PROMPT) - 1, -m);
				}
				continue;
			}

			buff[esc++] = c;

			if ((k = psh_keyCode(buff, &esc)) < 0) {
				if (k == -EINVAL) {
					if (hp != cmdhist->he) {
						psh_histentcmd(cmd, cmdhist->entries + hp);
						hp = cmdhist->he;
					}
					esc = 0;
				}
				/* EAGAIN */
				continue;
			}
			else if (k == kUp) {
				if (hp != cmdhist->hb) {
					if (hp == cmdhist->he)
						ln = n + m;
					psh_movecursor(n + sizeof(PROMPT) - 1, -(n + sizeof(PROMPT) - 1));
					write(STDOUT_FILENO, "\033[0J", 4);
					write(STDOUT_FILENO, PROMPT, sizeof(PROMPT) - 1);
					psh_printhistent(cmdhist->entries + (hp = (hp) ? hp - 1 : HISTSZ - 1));
					n = cmdhist->entries[hp].n;
					m = 0;
				}
			}
			else if (k == kDown) {
				if (hp != cmdhist->he) {
					psh_movecursor(n + sizeof(PROMPT) - 1, -(n + sizeof(PROMPT) - 1));
					write(STDOUT_FILENO, "\033[0J", 4);
					write(STDOUT_FILENO, PROMPT, sizeof(PROMPT) - 1);
					if ((hp = (hp + 1) % HISTSZ) == cmdhist->he) {
						n = ln;
						write(STDOUT_FILENO, *cmd, n);
					}
					else {
						n = cmdhist->entries[hp].n;
						psh_printhistent(cmdhist->entries + hp);
					}
					m = 0;
				}
			}
			else if (k == kRight) {
				if (m) {
					psh_movecursor(n + sizeof(PROMPT) - 1, 1);
					n++;
					m--;
				}
			}
			else if (k == kLeft) {
				if (n) {
					psh_movecursor(n + sizeof(PROMPT) - 1, -1);
					n--;
					m++;
				}
			}
			else if (k == kEnd) {
				if (m) {
					psh_movecursor(n + sizeof(PROMPT) - 1, m);
					n += m;
					m = 0;
				}
			}
			else if (k == kHome) {
				if (n) {
					psh_movecursor(n + sizeof(PROMPT) - 1, -n);
					m += n;
					n = 0;
				}
			}
			else if (k == kDelete) {
				if (m) {
					if (hp != cmdhist->he) {
						psh_histentcmd(cmd, cmdhist->entries + hp);
						hp = cmdhist->he;
					}
					memmove(*cmd + n, *cmd + n + 1, --m);
					write(STDOUT_FILENO, "\033[0J", 4);
					write(STDOUT_FILENO, *cmd + n, m);
					psh_movecursor(n + m + sizeof(PROMPT) - 1, -m);
				}
			}
		}
	}

	/* Restore original terminal settings */
	if ((esc = tcsetattr(STDIN_FILENO, TCSAFLUSH, orig)) < 0) {
		fprintf(stderr, "\r\npsh: failed to restore terminal settings\r\n");
		if (err >= 0)
			err = esc;
	}

	if (err < 0) {
		free(*cmd);
		return err;
	}

	if (*cmd == NULL) {
		return -ENODEV;
	}

	(*cmd)[n + m] = '\0';

	return n + m;
}


static int psh_parsecmd(char *line, int *argc, char ***argv)
{
	char *cmd, *arg, **rargv;

	if ((cmd = strtok(line, "\t ")) == NULL)
		return -EINVAL;

	if ((*argv = malloc(2 * sizeof(char *))) == NULL)
		return -ENOMEM;

	*argc = 0;
	(*argv)[(*argc)++] = cmd;

	while ((arg = strtok(NULL, "\t ")) != NULL) {
		if ((rargv = realloc(*argv, (*argc + 2) * sizeof(char *))) == NULL) {
			free(*argv);
			return -ENOMEM;
		}

		*argv = rargv;
		(*argv)[(*argc)++] = arg;
	}
	(*argv)[*argc] = NULL;

	return 0;
}


static int psh_runscript(char *path)
{
	char **argv = NULL, *line = NULL;
	int i, err, argc = 0;
	size_t n = 0;
	ssize_t ret;
	FILE *stream;
	pid_t pid;

	if ((stream = fopen(path, "r")) == NULL) {
		fprintf(stderr, "psh: failed to open file %s\n", path);
		return -EINVAL;
	}

	if ((getline(&line, &n, stream) < sizeof(SCRIPT_MAGIC)) || strncmp(line, SCRIPT_MAGIC, sizeof(SCRIPT_MAGIC) - 1)) {
		fprintf(stderr, "psh: %s is not a psh script\n", path);
		free(line);
		fclose(stream);
		return -EINVAL;
	}

	free(line);
	line = NULL;
	n = 0;

	for (i = 2; (ret = getline(&line, &n, stream)) > 0; i++) {
		if (line[0] == 'X' || line[0] == 'W' || line[0] == 'T') {
			if (line[ret - 1] == '\n')
				line[ret - 1] = '\0';

			do {
				if ((err = psh_parsecmd(line + 1, &argc, &argv)) < 0) {
					fprintf(stderr, "psh: failed to parse line %d\n", i);
					break;
				}

				/* TTY set */
				if (line[0] == 'T') {
					err = psh_ttyopen(argv[0]);
					break;
				}
				if ((pid = vfork()) < 0) {
					fprintf(stderr, "psh: vfork failed in line %d\n", i);
					err = pid;
					break;
				}
				else if (!pid) {
					execv(argv[0], argv);
					fprintf(stderr, "psh: exec failed in line %d\n", i);
					_psh_exit(EXIT_FAILURE);
				}

				if ((line[0] == 'W') && ((err = waitpid(pid, NULL, 0)) < 0)) {
					fprintf(stderr, "psh: waitpid failed in line %d\n", i);
					break;
				}
			} while (0);

			free(argv);
			argv = NULL;
		}

		if (err < 0)
			break;

		free(line);
		line = NULL;
		n = 0;
	}

	free(line);
	fclose(stream);

	return 0;
}


static void psh_historyhelp(void)
{
	printf("usage: history [options] or no args to print command history\n");
	printf("  -c:  clears command history\n");
	printf("  -h:  shows this help message\n");
}


void psh_historyinfo(void)
{
	printf("prints commands history");
}


int psh_history(int argc, char **argv)
{
	unsigned char clear = 0, help = 0;
	int c, i, size;
	psh_hist_t *cmdhist = pshapp_common.cmdhist;

	if (cmdhist == NULL) {
		fprintf(stderr, "psh: history not initialized\n");
		return EXIT_FAILURE;
	}

	/* Process options */
	if (argc > 1) {
		while ((c = getopt(argc, argv, "ch")) != -1) {
			switch (c) {
				case 'c':
					clear = 1;
					break;

				case 'h':
					help = 1;
					break;

				default:
					psh_historyhelp();
					return EXIT_FAILURE;
			}
		}

		/* History command doesn't take any arguments */
		if (optind < argc) {
			psh_historyhelp();
			return EXIT_FAILURE;
		}

		if (clear) {
			for (i = cmdhist->hb; i != cmdhist->he; i = (i + 1) % HISTSZ)
				free(cmdhist->entries[i].cmd);
			cmdhist->hb = cmdhist->he = 0;
		}

		if (help) {
			psh_historyhelp();
		}
	}
	/* Print history */
	else {
		size = (cmdhist->hb < cmdhist->he) ? cmdhist->he - cmdhist->hb : HISTSZ - cmdhist->hb + cmdhist->he;
		c = psh_log(10, size) + 1;

		for (i = 0; i < size; i++) {
			printf("  %*u  ", c, i + 1);
			fflush(stdout);
			psh_printhistent(cmdhist->entries + (cmdhist->hb + i) % HISTSZ);
			putchar('\n');
		}
	}

	return EXIT_SUCCESS;
}


static void psh_signalint(int sig)
{
	psh_common.sigint = 1;
}


static void psh_signalquit(int sig)
{
	psh_common.sigquit = 1;
}


static void psh_signalstop(int sig)
{
	psh_common.sigstop = 1;
}


static int psh_run(int exitable, const char *console)
{
	psh_hist_t *cmdhist = NULL;
	psh_histent_t *entry;
	const psh_appentry_t *app;
	struct termios orig;
	char *tmp, *cmd, **argv;
	int cnt, err, n, argc;
	pid_t pgrp;

	/* Time for klog to print data from buffer */
	usleep(500000);

	err = psh_ttyopen(console);
	if (err < 0) {
		return err;
	}

	/* Wait till we run in foreground */
	if (tcgetpgrp(STDIN_FILENO) != -1) {
		while ((pgrp = tcgetpgrp(STDIN_FILENO)) != getpgrp())
			if (kill(-pgrp, SIGTTIN) == 0)
				break;
	}

	/* Set signal handlers */
	signal(SIGINT, psh_signalint);
	signal(SIGQUIT, psh_signalquit);
	signal(SIGTSTP, psh_signalstop);
	signal(SIGTTIN, SIG_IGN);
	signal(SIGTTOU, SIG_IGN);
	signal(SIGCHLD, SIG_IGN);

	/* Check if this psh session is already in interactive mode */
	if (pshapp_common.cmdhist != NULL) {
		return -EPERM;
	}

	/* Put ourselves in our own process group */
	pgrp = getpid();
	if ((err = setpgid(pgrp, pgrp)) < 0) {
		fprintf(stderr, "psh: failed to put shell in its own process group\n");
		return err;
	}

	/* Save original terminal settings */
	if ((err = tcgetattr(STDIN_FILENO, &orig)) < 0) {
		fprintf(stderr, "psh: failed to save terminal settings\n");
		return err;
	}

	/* Take terminal control - only interactive psh should take control */
	if ((err = tcsetpgrp(STDIN_FILENO, pgrp)) < 0) {
		fprintf(stderr, "psh: failed to take terminal control\n");
		return err;
	}

	if ((cmdhist = calloc(1, sizeof(*cmdhist))) == NULL) {
		fprintf(stderr, "psh: failed to allocate command history storage\n");
		return -ENOMEM;
	}
	pshapp_common.cmdhist = cmdhist;

	while (pgrp == tcgetpgrp(STDIN_FILENO)) {
		write(STDOUT_FILENO, "\033[0J", 4);
		write(STDOUT_FILENO, PROMPT, sizeof(PROMPT) - 1);

		if ((n = psh_readcmd(&orig, cmdhist, &cmd)) < 0) {
			err = n;
			break;
		}

		if ((err = psh_parsecmd(cmd, &argc, &argv)) < 0) {
			free(cmd);
			if (err == -EINVAL)
				continue;
			break;
		}

		/* Select command history entry */
		if (cmdhist->he != cmdhist->hb) {
			entry = &cmdhist->entries[(cmdhist->he) ? cmdhist->he - 1 : HISTSZ - 1];
			if ((n == entry->n) && !memcmp(cmd, entry->cmd, n)) {
				cmdhist->he = (cmdhist->he) ? cmdhist->he - 1 : HISTSZ - 1;
				free(entry->cmd);
			}
			else {
				entry = cmdhist->entries + cmdhist->he;
			}
		}
		else {
			entry = cmdhist->entries + cmdhist->he;
		}

		/* Update command history */
		entry->cmd = cmd;
		entry->n = n;
		if ((cmdhist->he = (cmdhist->he + 1) % HISTSZ) == cmdhist->hb) {
			free(cmdhist->entries[cmdhist->hb].cmd);
			cmdhist->entries[cmdhist->hb].cmd = NULL;
			cmdhist->hb = (cmdhist->hb + 1) % HISTSZ;
		}

		/* Clear signals */
		psh_common.sigint = 0;
		psh_common.sigquit = 0;
		psh_common.sigstop = 0;

		/* Reset getopt */
		optind = 0;

		/* Find and run */
		app = psh_findapp(argv[0]);
		if (app == NULL) {

			/* Allow executable path start with "/", "./", "../" */
			cnt = 0;
			for (tmp = argv[0]; *tmp; tmp++) {
				if (*tmp != '.') {
					if (*tmp != '/') {
						cnt = 3;
					}
					break;
				}
				cnt++;
			}

			if (cnt < 3 && *tmp == '/') {
				app = psh_findapp("/");
			}
		}

		if (app != NULL) {
			err = app->run(argc, argv);
			psh_common.exitStatus = err;
		}
		else {
			err = PSH_UNKNOWN_CMD;
			psh_common.exitStatus = err;
			printf("Unknown command!\n");
		}

		free(argv);
		fflush(NULL);
	}

	/* Free command history */
	for (; cmdhist->hb != cmdhist->he; cmdhist->hb = (cmdhist->hb + 1) % HISTSZ)
		free(cmdhist->entries[cmdhist->hb].cmd);

	free(cmdhist);
	pshapp_common.cmdhist = NULL;

	return err;
}


int psh_pshappexit(int argc, char **argv)
{
	tcsetpgrp(STDIN_FILENO, -1);
	return 0;
}


void psh_pshappexitinfo(void)
{
	printf("exits shell");
}


int psh_pshapp(int argc, char **argv)
{
	char *path = NULL;
	const char *consolePath = _PATH_CONSOLE;
	int c;

	/* Run shell script */
	if (argc > 1) {
		while ((c = getopt(argc, argv, "t:i:h")) != -1) {
			switch (c) {
				case 't':
					consolePath = optarg;
					break;
				case 'i':
					path = optarg;
					break;

				case 'h':
				default:
					printf("usage: %s [options] [script path] or no args to run shell interactively\n", argv[0]);
					printf("  -i <script path>:   selects psh script to execute\n");
					printf("  -t <terminal dev>:  path to terminal device, default %s\n", _PATH_CONSOLE);
					printf("  -h:                 shows this help message\n");
					return EXIT_SUCCESS;
			}
		}

		if (optind < argc)
			path = argv[optind];

		if (path != NULL)
			return psh_runscript(path) < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
	}
	/* Run shell interactively */
	else {
		return psh_run(1, consolePath) < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
	}

	return EXIT_FAILURE;
}


void __attribute__((constructor)) pshapp_registerapp(void)
{
	static psh_appentry_t app_pshapp = { .name = "psh", .run = psh_pshapp, .info = NULL };
	static psh_appentry_t app_pshappexit = { .name = "exit", .run = psh_pshappexit, .info = psh_pshappexitinfo };
	static psh_appentry_t app_pshlogin = { .name = "pshlogin", .run = psh_pshapp, .info = NULL };
	static psh_appentry_t app_pshhistory = { .name = "history", .run = psh_history, .info = psh_historyinfo };

	psh_registerapp(&app_pshapp);
	psh_registerapp(&app_pshappexit);
	psh_registerapp(&app_pshlogin);
	psh_registerapp(&app_pshhistory);
}
