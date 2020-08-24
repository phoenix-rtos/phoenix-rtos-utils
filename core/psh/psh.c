/*
 * Phoenix-RTOS
 *
 * libphoenix
 *
 * test/psh
 *
 * Copyright 2017, 2018 Phoenix Systems
 * Author: Pawel Pisarczyk, Jan Sikorski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/threads.h>
#include <sys/minmax.h>
#include <sys/mman.h>
#include <sys/msg.h>
#include <sys/mount.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/reboot.h>
#include <arch.h>
#include "top.h"
#include "psh.h"

#define PSH_SCRIPT_MAGIC ":{}:"


static int psh_mod(int x, int y)
{
	int res = x % y;

	if (res < 0)
		res += abs(y);

	return res;
}


static int psh_div(int x, int y)
{
	return (x - psh_mod(x, y)) / y;
}


static int psh_log(unsigned int base, unsigned int x)
{
	int res = 0;

	while (x /= base)
		res++;

	return res;
}


static int psh_pow(int x, unsigned int y)
{
	int res = 1;

	while (y) {
		if (y & 1)
			res *= x;
		y >>= 1;
		if (!y)
			break;
		x *= x;
	}

	return res;
}


static int psh_isNewline(char c)
{
	if (c == '\r' || c == '\n')
		return 1;

	return 0;
}


static int psh_isAcceptable(char c)
{
	if (psh_isNewline(c))
		return 1;

	return (c >= ' ');
}


char *psh_nextString(char *buff, unsigned int *size)
{
	char *s, *p;

	/* Skip leading spaces. */
	for (s = buff; *s == ' '; ++s);

	/* Count string size. */
	for (p = s, *size = 0; *p && *p != ' '; ++p, ++(*size));

	s[*size] = '\0';

	return s;
}


static int psh_readln(char *line, int size)
{
	int count = 0;
	char c;

	for (;;) {
		read(0, &c, 1);

		if (c == 0) /* EOF - exit */
			exit(EXIT_SUCCESS);

		if (!psh_isAcceptable(c))
			continue;

		/* Check, if line has maximum size. */
		if (count >= size - 2 && c != 0x7f && !psh_isNewline(c))
			continue;

		if (psh_isNewline(c))
			break;

		line[count++] = c;
	}

	memset(&line[count], '\0', size - count);

	return count;
}


static char *psh_BP(int exp)
{
	#define PSH_BP_OFFSET     0
	#define PSH_BP_EXP_OFFSET 10

	/* Binary prefixes */
	static char *BP[] = {
		"",   /* 2^0       */
		"K",  /* 2^10   kibi */
		"M",  /* 2^20   mebi */
		"G",  /* 2^30   gibi */
		"T",  /* 2^40   tebi */
		"P",  /* 2^50   pebi */
		"E",  /* 2^60   exbi */
		"Z",  /* 2^70   zebi */
		"Y"   /* 2^80   yobi */
	};

	exp = psh_div(exp, PSH_BP_EXP_OFFSET) + PSH_BP_OFFSET;

	if (exp < 0 || exp >= sizeof(BP) / sizeof(char *))
		return NULL;

	return BP[exp];
}


static char *psh_SI(int exp)
{
	#define PSH_SI_OFFSET     8
	#define PSH_SI_EXP_OFFSET 3

	/* SI prefixes */
	static char* SI[] = {
		"y",  /* 10^-24 yocto */
		"z",  /* 10^-21 zepto */
		"a",  /* 10^-18 atto  */
		"f",  /* 10^-15 femto */
		"p",  /* 10^-12 pico  */
		"n",  /* 10^-9  nano  */
		"u",  /* 10^-6  micro */
		"m",  /* 10^-3  milli */
		"",   /* 10^0         */
		"k",  /* 10^3   kilo  */
		"M",  /* 10^6   mega  */
		"G",  /* 10^9   giga  */
		"T",  /* 10^12  tera  */
		"P",  /* 10^15  peta  */
		"E",  /* 10^18  exa   */
		"Z",  /* 10^21  zetta */
		"Y",  /* 10^24  yotta */
	};

	exp = psh_div(exp, PSH_SI_EXP_OFFSET) + PSH_SI_OFFSET;

	if (exp < 0 || exp >= sizeof(SI) / sizeof(char *))
		return NULL;

	return SI[exp];
}


/* Convert n = x * base^y to a short binary(base 2)/SI(base 10) prefix notation */
/* (value of n gets rounded to prec decimal places, trailing zeros get cut), e.g. */
/* psh_convert(SI, -15496, 3, 2, buff) saves "-15.5M" in buff */
/* psh_convert(BP, 2000, 10, 3, buff) saves "1.953M" in buff */
int psh_convert(unsigned int base, int x, int y, unsigned int prec, char *buff)
{
	char *(*fp)(int);
	char *prefix;
	char fmt[11];
	int offset, ipart, fpart;
	int div = psh_log(base, abs(x));
	int exp = div + y;

	/* Support precision for up to 8 decimal places */
	if (prec > 8)
		return -1;

	switch (base) {
	/* Binary prefix */
	case BP:
		fp = psh_BP;
		offset = PSH_BP_EXP_OFFSET;
		break;

	/* SI prefix */
	case SI:
		fp = psh_SI;
		offset = PSH_SI_EXP_OFFSET;
		break;

	default:
		return -1;
	}

	/* div < 0 => accumulate extra exponents in x */
	if ((div -= psh_mod(exp, offset)) < 0) {
		x *= psh_pow(base, -div);
		div = 0;
	}
	div = psh_pow(base, div);

	/* Save integer part */
	ipart = abs(x) / div;
	/* Save fractional part as percentage */
	fpart = (int)((uint64_t)psh_pow(10, prec + 1) * (abs(x) % div) / div);
	/* Round the result */
	if ((fpart = (fpart + 5) / 10) == psh_pow(10, prec)) {
		ipart++;
		fpart = 0;
		if (ipart == psh_pow(base, offset)) {
			ipart = 1;
			exp += offset;
		}
	}

	/* Remove trailing zeros */
	while (fpart && !(fpart % 10)) {
		fpart /= 10;
		prec--;
	}

	/* Get the prefix */
	if (!ipart && !fpart)
		prefix = fp(y);
	else
		prefix = fp(exp);

	if (prefix == NULL)
		return -1;

	if (x < 0)
		*buff++ = '-';

	if (fpart) {
		sprintf(fmt, "%%d.%%0%ud%%s", prec);
		sprintf(buff, fmt, ipart, fpart, prefix);
	}
	else {
		sprintf(buff, "%d%s", ipart, prefix);
	}

	return 0;
}


static void psh_help(void)
{
	printf("Available commands:\n");
	printf("  cat    - concatenates files\n");
	printf("  exec   - executes a file\n");
	printf("  exit   - exits the shell\n");
	printf("  help   - prints this help\n");
	printf("  ls     - lists files in the namespace\n");
	printf("  mem    - prints memory map\n");
	printf("  mkdir  - creates directory\n");
	printf("  ps     - prints list of processes and threads\n");
	printf("  top    - top utility\n");
	printf("  touch  - changes file timestamp\n");
}


static void psh_mkdir(char *args)
{
	char *path = args;
	unsigned int len;

	while ((path = psh_nextString(path, &len)) && len) {
		if (mkdir(path, 0) < 0) {
			printf("%s: failed to create directory\n", path);
		}

		path += len + 1;
	}
}


static void psh_touch(char *args)
{
	char *path = args;
	unsigned int len;
	FILE *f;

	while ((path = psh_nextString(path, &len)) && len) {
		if ((f = fopen(path, "w")) == NULL) {
			printf("%s: fopen failed\n", path);
		}
		else {
			fclose(f);
		}

		path += len + 1;
	}
}


static int psh_mem(char *args)
{
	char *arg, *end;
	unsigned int len, i, n;
	meminfo_t info;
	int mapsz = 0;
	entryinfo_t *e = NULL;
	pageinfo_t *p = NULL;
	char flags[8], prot[5], *f, *r;
	void *map_re;

	memset(&info, 0, sizeof(info));
	arg = psh_nextString(args, &len);
	args += len + 1;

	if (!len) {
		/* show summary */
		info.page.mapsz = -1;
		info.entry.mapsz = -1;
		info.entry.kmapsz = -1;

		meminfo(&info);

		printf("(%d+%d)/%dKB ", (info.page.alloc - info.page.boot) / 1024, info.page.boot / 1024,
			(info.page.alloc + info.page.free) / 1024);

		printf("%d/%d entries\n", info.entry.total - info.entry.free, info.entry.total);

		return EOK;
	}

	if (!strcmp("-m", arg)) {
		info.page.mapsz = -1;
		arg = psh_nextString(args, &len);

		if (!strcmp("kernel", arg))  {
			/* show memory map of the kernel */
			info.entry.mapsz = -1;
			info.entry.kmapsz = 16;

			do {
				mapsz = info.entry.kmapsz;
				if ((map_re = realloc(info.entry.kmap, mapsz * sizeof(entryinfo_t))) == NULL) {
					printf("psh: out of memory\n");
					free(info.entry.kmap);
					return -ENOMEM;
				}
				info.entry.kmap = map_re;
				meminfo(&info);
			}
			while (info.entry.kmapsz > mapsz);

			mapsz = info.entry.kmapsz;
			e = info.entry.kmap;
		}
		else {
			/* show memory map of a process */
			if (len) {
				info.entry.pid = strtoul(arg, &end, 10);

				if (end != args + len || (!info.entry.pid && *arg != '0')) {
					printf("mem: could not parse process id: '%s'\n", arg);
					return EOK;
				}
			}
			else {
				info.entry.pid = getpid();
			}

			info.entry.kmapsz = -1;
			info.entry.mapsz = 16;

			do {
				mapsz = info.entry.mapsz;
				if ((map_re = realloc(info.entry.map, mapsz * sizeof(entryinfo_t))) == NULL) {
					printf("psh: out of memory\n");
					free(info.page.map);
					return -ENOMEM;
				}
				info.entry.map = map_re;
				meminfo(&info);
			}
			while (info.entry.mapsz > mapsz);

			if (info.entry.mapsz < 0) {
				printf("mem: process with pid %x not found\n", info.entry.pid);
				free(info.entry.map);
				return EOK;
			}

			mapsz = info.entry.mapsz;
			e = info.entry.map;
		}

		printf("%-17s  PROT  FLAGS  %16s  OBJECT\n", "SEGMENT", "OFFSET");

		e += mapsz - 1;
		for (i = 0; i < mapsz; ++i, --e) {
			memset(f = flags, 0, sizeof(flags));

			if (e->flags & MAP_NEEDSCOPY)
				*(f++) = 'C';

			if (e->flags & MAP_PRIVATE)
				*(f++) = 'P';

			if (e->flags & MAP_FIXED)
				*(f++) = 'F';

			if (e->flags & MAP_ANONYMOUS)
				*(f++) = 'A';

			memset(r = prot, 0, sizeof(prot));

			if (e->prot & PROT_READ)
				*(r++) = 'r';
			else
				*(r++) = '-';

			if (e->prot & PROT_WRITE)
				*(r++) = 'w';
			else
				*(r++) = '-';

			if (e->prot & PROT_EXEC)
				*(r++) = 'x';
			else
				*(r++) = '-';

			*(r++) = '-';

			printf("%p:%p  %4s  %5s", e->vaddr, e->vaddr + e->size - 1, prot, flags);

			if (e->offs != -1)
				printf("  %16llx", e->offs);

			else
				printf("  %16s", "");

			if (e->object == OBJECT_ANONYMOUS)
				printf("  %s", "(anonymous)");

			else if (e->object == OBJECT_MEMORY)
				printf("  %s", "mem");

			else
				printf("  %d.%llu", e->oid.port, e->oid.id);

			if (e->object != OBJECT_ANONYMOUS && e->anonsz != ~0)
				printf("/(%zuKB)\n", e->anonsz / 1024);

			else
				printf("\n");
		}

		free(info.entry.map);
		free(info.entry.kmap);

		return EOK;
	}

	if (!strcmp("-p", arg)) {
		/* show page map */
		info.entry.mapsz = ~0;
		info.entry.kmapsz = ~0;
		info.page.mapsz = 16;

		do {
			mapsz = info.page.mapsz;
			if ((map_re = realloc(info.page.map, mapsz * sizeof(pageinfo_t))) == NULL) {
				printf("psh: out of memory\n");
				free(info.page.map);
				return -ENOMEM;
			}
			info.page.map = map_re;
			meminfo(&info);
		}
		while (info.page.mapsz > mapsz);

		for (i = 0, p = info.page.map; i < info.page.mapsz; ++i, ++p) {
			if (p != info.page.map && (n = (p->addr - (p - 1)->addr) / _PAGE_SIZE - (p - 1)->count)) {
				if (n > 3) {
					printf("[%ux]", n);
				}
				else {
					while (n-- > 0)
						printf("x");
				}
			}

			if ((n = p->count) > 3) {
				printf("[%u%c]", p->count, p->marker);
				continue;
			}

			while (n-- > 0)
				printf("%c", p->marker);
		}
		printf("\n");

		free(info.page.map);
		return EOK;
	}

	printf("mem: unrecognized option '%s'\n", arg);
	return EOK;
}


static int psh_perf(char *args)
{
	char *timeout_s;
	unsigned len;
	time_t timeout, elapsed = 0, sleeptime = 200 * 1000;
	perf_event_t *buffer;
	const size_t bufsz = 4 << 20;
	int bcount, tcnt, n = 32;
	threadinfo_t *info, *info_re;

	if ((info = malloc(n * sizeof(threadinfo_t))) == NULL) {
		fprintf(stderr, "perf: out of memory\n");
		return -ENOMEM;
	}

	while ((tcnt = threadsinfo(n, info)) >= n) {
		n *= 2;
		if ((info_re = realloc(info, n * sizeof(threadinfo_t))) == NULL) {
			free(info);
			fprintf(stderr, "perf: out of memory\n");
			return -ENOMEM;
		}

		info = info_re;
	}

	if (fwrite(&tcnt, sizeof(tcnt), 1, stdout) != 1) {
		fprintf(stderr, "perf: failed or partial write\n");
		free(info);
		return -1;
	}

	if (fwrite(info, sizeof(threadinfo_t), tcnt, stdout) != tcnt) {
		fprintf(stderr, "perf: failed or partial write\n");
		free(info);
		return -1;
	}

	free(info);

	timeout_s = psh_nextString(args, &len);
	args += len + 1;

	timeout = 1000 * 1000 * strtoull(timeout_s, NULL, 10);

	buffer = malloc(bufsz);

	if (buffer == NULL) {
		fprintf(stderr, "perf: out of memory\n");
		return -ENOMEM;
	}

	if (perf_start(-1) < 0) {
		fprintf(stderr, "perf: could not start\n");
		free(buffer);
		return -1;
	}

	while (elapsed < timeout) {
		bcount = perf_read(buffer, bufsz);

		if (fwrite(buffer, 1, bcount, stdout) < bcount) {
			fprintf(stderr, "perf: failed or partial write\n");
			break;
		}

		fprintf(stderr, "perf: wrote %d/%d bytes\n", bcount, bufsz);

		usleep(sleeptime);
		elapsed += sleeptime;
	}

	perf_finish();
	free(buffer);
	return EOK;
}


static int psh_ps_cmp_name(const void *t1, const void *t2)
{
	return strcmp(((threadinfo_t *)t1)->name, ((threadinfo_t *)t2)->name);
}


static int psh_ps_cmp_pid(const void *t1, const void *t2)
{
	return (int)((threadinfo_t *)t1)->pid - (int)((threadinfo_t *)t2)->pid;
}


static int psh_ps_cmp_cpu(const void *t1, const void *t2)
{
	return ((threadinfo_t *)t2)->load - ((threadinfo_t *)t1)->load;
}


static int psh_ps(char *arg)
{
	threadinfo_t *info, *info_re;
	unsigned int h, m, len;
	int tcnt, i, j, n = 32;
	int collapse_threads = 1;
	char buff[8];
	int (*cmp)(const void *, const void*) = psh_ps_cmp_pid;

	if ((info = malloc(n * sizeof(threadinfo_t))) == NULL) {
		printf("ps: out of memory\n");
		return -ENOMEM;
	}

	while ((tcnt = threadsinfo(n, info)) >= n) {
		n *= 2;
		if ((info_re = realloc(info, n * sizeof(threadinfo_t))) == NULL) {
			free(info);
			printf("ps: out of memory\n");
			return -ENOMEM;
		}
		info = info_re;
	}

	while ((arg = psh_nextString(arg, &len)) && len) {
		if (!strcmp(arg, "-p")) {
			cmp = psh_ps_cmp_pid;
		}
		else if (!strcmp(arg, "-n")) {
			cmp = psh_ps_cmp_name;
		}
		else if (!strcmp(arg, "-c")) {
			cmp = psh_ps_cmp_cpu;
		}
		else if (!strcmp(arg, "-t")) {
			collapse_threads = 0;
		}
		else {
			printf("ps: unknown option '%s'\n", arg);
			free(info);
			return EOK;
		}

		arg += len + 1;
	}

	if (collapse_threads) {
		qsort(info, tcnt, sizeof(threadinfo_t), psh_ps_cmp_pid);
		for (i = 0; i < tcnt; i++) {
			info[i].tid = 1;

			for (j = i + 1; j < tcnt && info[j].pid == info[i].pid; j++) {
				info[i].tid++;
				info[i].load += info[j].load;
				info[i].cpuTime += info[j].cpuTime;
				info[i].priority = min(info[i].priority, info[j].priority);
				info[i].state = min(info[i].state, info[j].state);
				info[i].wait = max(info[i].wait, info[j].wait);
			}

			if (j > i + 1) {
				memcpy(info + i + 1, info + j, (tcnt - j) * sizeof(threadinfo_t));
				tcnt -= j - i - 1;
			}
		}
		printf("%5s %5s %2s %5s %5s %5s %7s %6s %3s %-28s\n", "PID", "PPID", "PR", "STATE", "%CPU", "WAIT", "TIME", "VMEM", "THR", "CMD");
	}
	else {
		printf("%5s %5s %2s %5s %5s %5s %7s %6s %-32s\n", "PID", "PPID", "PR", "STATE", "%CPU", "WAIT", "TIME", "VMEM", "CMD");
	}

	qsort(info, tcnt, sizeof(threadinfo_t), cmp);

	for (i = 0; i < tcnt; i++) {
		psh_convert(SI, info[i].wait, -6, 1, buff);
		info[i].cpuTime /= 10000;
		h = info[i].cpuTime / 3600;
		m = (info[i].cpuTime - h * 3600) / 60;
		printf("%5u %5u %2d %5s %3u.%u %4ss %4u:%02u ", info[i].pid, info[i].ppid, info[i].priority, (info[i].state) ? "sleep" : "ready",
			info[i].load / 10, info[i].load % 10, buff, h, m);

		psh_convert(BP, info[i].vmem, 0, 1, buff);
		printf("%6s ", buff);

		if (collapse_threads)
			printf("%3u %-28s\n", info[i].tid, info[i].name);
		else
			printf("%-32s\n", info[i].name);
	}

	free(info);
	return EOK;
}


int psh_exec(char *cmd)
{
	int exerr = 0;
	int argc = 0;
	char **argv = NULL, **argv_re;

	char *arg = cmd;
	unsigned int len;

	while ((arg = psh_nextString(arg, &len)) && len) {
		if ((argv_re = realloc(argv, (2 + argc) * sizeof(char *))) == NULL) {
			free(argv);
			printf("psh: out of memory\n");
			return -ENOMEM;
		}
		argv = argv_re;
		argv[argc++] = arg;
		arg += len + 1;
	}
	if (argc == 0) {
		printf("psh: exec empty agument\n");
		return -EINVAL;
	}

	argv[argc] = NULL;

	exerr = execve(cmd, argv, NULL);

	if (exerr == -ENOMEM)
		printf("psh: not enough memory to exec\n");

	else if (exerr == -EINVAL)
		printf("psh: invalid executable\n");

	else if (exerr < 0)
		printf("psh: exec failed with code %d\n", exerr);

	return exerr;
}


int psh_runfile(char *cmd)
{
	volatile int exerr = EOK;
	int pid;
	int argc = 0;
	char **argv = NULL, **argv_re;

	char *arg = cmd;
	unsigned int len;

	while ((arg = psh_nextString(arg, &len)) && len) {
		if ((argv_re = realloc(argv, (2 + argc) * sizeof(char *))) == NULL) {
			free(argv);
			printf("psh: out of memory\n");
			return -ENOMEM;
		}
		argv = argv_re;
		argv[argc++] = arg;
		arg += len + 1;
	}

	argv[argc] = NULL;

	if ((pid = vfork()) < 0) {
		printf("psh: vfork failed\n");
		return pid;
	}
	else if (!pid) {
		exit(exerr = execve(cmd, argv, NULL));
	}

	if (exerr == EOK)
		return wait(0);

	if (exerr == -ENOMEM)
		printf("psh: not enough memory to exec\n");

	else if (exerr == -EINVAL)
		printf("psh: invalid executable\n");

	else
		printf("psh: exec failed with code %d\n", exerr);

	return exerr;
}


int psh_cat(char *args)
{
	char *arg = args, *buf;
	int rsz;
	unsigned int len;
	FILE *file;

	if ((buf = malloc(1024)) == NULL) {
		printf("cat: out of memory\n");
		return -ENOMEM;
	}

	while ((arg = psh_nextString(arg, &len)) && len) {
		file = fopen(arg, "r");

		if (file == NULL) {
			printf("cat: %s no such file\n", arg);
		}
		else {
			while ((rsz = fread(buf, 1, 1024, file)) > 0) {
				fwrite(buf, 1, rsz, stdout);
			}
		}

		fclose(file);
		arg += len + 1;
	}

	free(buf);
	return EOK;
}


static int psh_kill(char *arg)
{
	unsigned len, pid;
	char *end;

	arg = psh_nextString(arg, &len);

	if (!len) {
		printf("kill: missing argument (pid)\n");
		return -EINVAL;
	}

	pid = strtoul(arg, &end, 10);

	if ((end != arg + len) || (pid == 0 && *arg != '0')) {
		printf("kill: could not parse process id: '%s'\n", arg);
		return -EINVAL;
	}

	return signalPost(pid, -1, signal_kill);
}


static int psh_mount(int argc, char **argv)
{
	int err;

	if (argc != 5 && argc != 4) {
		printf("usage: mount source target fstype mode %d\n", argc);
		return -1;
	}

	err = mount(argv[0], argv[1], argv[2], atoi(argv[3]), argv[4]);

	if (err < 0)
		printf("mount: %s\n", strerror(err));

	return err;
}


static int psh_bind(int argc, char **argv)
{
	struct stat buf;
	oid_t soid, doid;
	msg_t msg = {0};
	int err;

	if (argc != 2) {
		printf("usage: bind source target %d\n", argc);
		return -1;
	}

	if (lookup(argv[0], NULL, &soid) < EOK)
		return -ENOENT;

	if (lookup(argv[1], NULL, &doid) < EOK)
		return -ENOENT;

	if ((err = stat(argv[1], &buf)))
		return err;

	if (!S_ISDIR(buf.st_mode))
		return -ENOTDIR;

	msg.type = mtSetAttr;
	msg.i.attr.oid = doid;
	msg.i.attr.type = atDev;
	msg.i.data = &soid;
	msg.i.size = sizeof(oid_t);

	if ((err = msgSend(doid.port, &msg)) < 0)
		return err;

	return msg.o.attr.val;

}


static int psh_sync(int argc, char **argv)
{
	oid_t oid;
	msg_t msg = {0};
	msg.type = mtSync;

	if (!argc) {
		printf("usage: sync path-to-device\n");
		return -1;
	}

	if (lookup(argv[0], NULL, &oid) < 0)
		return -1;

	return msgSend(oid.port, &msg);
}


static void psh_reboot(char *arg)
{
	unsigned int len, magic = PHOENIX_REBOOT_MAGIC;

#ifdef TARGET_IMX6ULL
	if (arg != NULL) {
		arg = psh_nextString(arg, &len);
		if (len && !strcmp(arg, "-s"))
			magic = ~magic;
	}
#else
	(void)len;
#endif

	reboot(magic);
}


void psh_run(void)
{
	unsigned int n;
	char buff[128];
	char *cmd;

	for (;;) {
		write(1, "(psh)% ", 7);

		psh_readln(buff, sizeof(buff));
		cmd = psh_nextString(buff, &n);

		if (n == 0)
			continue;

		if (!strcmp(cmd, "help"))
			psh_help();

		else if (!strcmp(cmd, "ls"))
			psh_ls(cmd + n + 1);

		else if (!strcmp(cmd, "mem"))
			psh_mem(cmd + n + 1);

		else if (!strcmp(cmd, "ps"))
			psh_ps(cmd + n + 1);

		else if (!strcmp(cmd, "cat"))
			psh_cat(cmd + n + 1);

		else if (!strcmp(cmd, "touch"))
			psh_touch(cmd + n + 1);

		else if (!strcmp(cmd, "mkdir"))
			psh_mkdir(cmd + n + 1);

		else if (!strcmp(cmd, "exec"))
			psh_exec(cmd + n + 1);

		else if (!strcmp(cmd, "kill"))
			psh_kill(cmd + n + 1);

		else if (!strcmp(cmd, "perf"))
			psh_perf(cmd + n + 1);

		else if (!strcmp(cmd, "top"))
			psh_top(cmd + n + 1);

		else if (cmd[0] == '/')
			psh_runfile(cmd);

		else if (!strcmp(cmd, "exit"))
			exit(EXIT_SUCCESS);

		else if (!strcmp(cmd, "reboot"))
			psh_reboot(cmd + n + 1);

		else
			printf("Unknown command!\n");
		fflush(NULL);
	}
}


int psh_runscript(char *path)
{
	FILE *stream;
	char *line = NULL;
	size_t n = 0;
	char *arg;
	int argc = 0;
	char **argv = NULL, **argv_re;
	char *bin;
	int err;
	pid_t pid;

	stream = fopen(path, "r");

	if (stream == NULL)
		return -EINVAL;
	if (getline(&line, &n, stream) == 5) {
		if (strncmp(PSH_SCRIPT_MAGIC, line, strlen(PSH_SCRIPT_MAGIC))) {
			free(line);
			fclose(stream);
			printf("psh: %s is not a psh script\n", path);
			return -1;
		}
	}

	free(line);
	line = NULL;
	n = 0;

	while (getline(&line, &n, stream) > 0) {

		if (line[0] == 'X' || line[0] == 'W') {
			strtok(line, " ");

			bin = strtok(NULL, " ");
			if (bin == NULL) {
				free(line);
				fclose(stream);
				line = NULL;
				n = 0;
				return -1;
			}

			if (bin[strlen(bin) - 1] == '\n')
				bin[strlen(bin) - 1] = 0;

			argv = malloc(2 * sizeof(char *));
			argv[argc++] = bin;

			while ((arg = strtok(NULL, " ")) != NULL) {
				if (arg[strlen(arg) - 1] == '\n')
					arg[strlen(arg) - 1] = 0;

				argv_re = realloc(argv, (argc + 2) * sizeof(char *));

				if (argv_re == NULL) {
					printf("psh: Out of memory\n");
					free(argv);
					free(line);
					fclose(stream);
					return -1;
				}

				argv = argv_re;
				argv[argc] = arg;
				argc++;
			}

			argv[argc] = NULL;

			if (!(pid = vfork())) {
				err = execve(bin, argv, NULL);
				printf("psh: execve failed %d\n", err);
				exit(err);
			}

			if (line[0] == 'W')
				waitpid(pid, NULL, 0);
		}

		free(argv);
		free(line);
		argv = NULL;
		argc = 0;
		line = NULL;
		n = 0;
	}
	fclose(stream);
	return EOK;
}


extern void splitname(char *, char **, char**);


int main(int argc, char **argv)
{
	int c;
	oid_t oid;
	char *args;
	char *base, *dir, *cmd;
	FILE *file;

	splitname(argv[0], &base, &dir);

	if (!strcmp(base, "psh")) {
		/* Wait for filesystem */
		while (lookup("/", NULL, &oid) < 0)
			usleep(10000);

		/* Wait for console */
		while (write(1, "", 0) < 0)
			usleep(50000);

		if (argc > 0 && (c = getopt(argc, argv, "i:")) != -1) {
			if (psh_runscript(optarg) != EOK)
				printf("psh: error during preinit\n");

			file = fopen("/var/preinit", "w+");

			if (file != NULL) {
				while ((cmd = argv[optind++]) != NULL) {
					fwrite(cmd, 1, strlen(cmd), file);
					fwrite(" ", 1, 1, file);
				}

				fwrite("\n", 1, 1, file);
				fclose(file);
			}
		}
		else {
			psh_run();
		}
	}
	else {
		if ((args = calloc(3000, 1)) == NULL) {
			printf("psh: out of memory\n");
			return EXIT_FAILURE;
		}

		for (c = 1; c < argc; ++c) {
			strcat(args, argv[c]);
			strcat(args, " ");
		}

		if (!strcmp(base, "mem"))
			psh_mem(args);
		else if (!strcmp(base, "ps"))
			psh_ps(args);
		else if (!strcmp(base, "perf"))
			psh_perf(args);
		else if (!strcmp(base, "mount"))
			psh_mount(argc - 1, argv + 1);
		else if (!strcmp(base, "bind"))
			psh_bind(argc - 1, argv + 1);
		else if (!strcmp(base, "sync"))
			psh_sync(argc - 1, argv + 1);
		else if (!strcmp(base, "reboot"))
			psh_reboot(argv[1]);
		else if(!strcmp(base, "top"))
			psh_top(args);
		else
			printf("psh: %s: unknown command\n", argv[0]);
	}

	return 0;
}
