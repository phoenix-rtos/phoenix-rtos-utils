/*
 * Phoenix-RTOS
 *
 * Phoenix Shell top
 *
 * Copyright 2020 Phoenix Systems
 * Author: Maciej Purski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/threads.h>
#include <unistd.h>
#include <termios.h>
#include <sys/time.h>
#include <sys/minmax.h>
#include <sys/ioctl.h>
#include <math.h>
#include <time.h>
#include "psh.h"

#define ASC 1
#define DESC -1


static int sortdir;
static int threads_mode;
static struct winsize w;
static int (*cmp)(const void *, const void*);


static int top_cmpcpu(const void *t1, const void *t2)
{
	return (((threadinfo_t *)t1)->load - ((threadinfo_t *)t2)->load) * sortdir;
}

static int top_cmppid(const void *t1, const void *t2)
{
	if (threads_mode)
		return ((int)((threadinfo_t *)t1)->tid - (int)((threadinfo_t *)t2)->tid) * sortdir;
	else
		return ((int)((threadinfo_t *)t1)->pid - (int)((threadinfo_t *)t2)->pid) * sortdir;
}

static int top_cmptime(const void *t1, const void *t2)
{
	return ((int)((threadinfo_t *)t1)->cpuTime - (int)((threadinfo_t *)t2)->cpuTime) * sortdir;
}

static int top_cmpmem(const void *t1, const void *t2)
{
	return ((int)((threadinfo_t *)t1)->vmem - (int)((threadinfo_t *)t2)->vmem) * sortdir;
}

static void top_printHelp(void)
{
	static const char help[] = "Command line arguments:\n"
				   "  -h:  Prints help.\n"
				   "  -H:  Starts with threads mode.\n"
				   "  -d:  Set refresh rate. Integer greater than 0.\n"
				   "  -n:  Set number of iterations. By default its infinity.\n\n"
				   "Interactive commands:\n"
				   "  <ENTER> or <SPACE>: Refresh\n"
				   "  H:  Toggle threads mode\n"
				   "  q:  Quit\n"
				   "  P:  Sort by CPU\n"
				   "  M:  Sort by MEM\n"
				   "  T:  Sort by TIME\n"
				   "  N:  Sort by PID\n"
				   "  R:  Reverse sorting\n";

	printf(help);
}

/* Enables/disables canon mode and echo */
static void top_termSwitchMode(int canon)
{
	struct termios ttystate;

	tcgetattr(STDIN_FILENO, &ttystate);
	if (canon) {
		ttystate.c_lflag |= ICANON;
		ttystate.c_lflag |= ECHO;
	} else {
		ttystate.c_lflag &= ~ICANON;
		ttystate.c_lflag &= ~ECHO;
		ttystate.c_cc[VMIN] = 1;
	}

	tcsetattr(STDIN_FILENO, TCSANOW, &ttystate);
}

/* Interruptible wait for command */
static int top_cmdwait(unsigned int secs)
{
	struct timeval left;
	fd_set fds;

	left.tv_sec = (time_t) secs;
	left.tv_usec = 0;
	fflush(stdout);
	FD_ZERO(&fds);
	FD_SET(STDIN_FILENO, &fds);
	select(STDIN_FILENO + 1, &fds, NULL, NULL, &left);

	if (FD_ISSET(STDIN_FILENO, &fds))
		return getchar();

	return 0;
}

static void top_refresh(char cmd, threadinfo_t *info, threadinfo_t *prev_info,
			unsigned int totcnt, time_t delta)
{
	static unsigned int prevlines = 0;
	static unsigned int prevcnt = 0;
	unsigned int lines = 3;
	unsigned int runcnt = 0, waitcnt = 0;
	unsigned int i, j;
	unsigned int m, s, hs;
	char buff[8];

	/* Calculate load */
	for (i = 0; i < totcnt; i++) {
		threadinfo_t *p = NULL;
		for (j = 0; j < prevcnt; j++) {
			if (info[i].tid == prev_info[j].tid) {
				p = &prev_info[j];
				break;
			}
		}
		if (p) {
			/* Prevent negative load, if a new thread with
			 * the same tid has occured */
			if (info[i].cpuTime >= p->cpuTime)
				info[i].load = ((info[i].cpuTime - p->cpuTime)) * 1000 / delta;
		}
	}

	prevcnt = totcnt;
	memcpy(prev_info, info, totcnt * sizeof(threadinfo_t));

	if (!threads_mode) {
		qsort(info, totcnt, sizeof(threadinfo_t), top_cmppid);
		for (i = 0; i < totcnt; i++) {
			info[i].tid = 1;

			for (j = i + 1; j < totcnt && info[j].pid == info[i].pid; j++) {
				info[i].tid++;
				info[i].load += info[j].load;
				info[i].cpuTime += info[j].cpuTime;
				info[i].priority = min(info[i].priority, info[j].priority);
				info[i].state = min(info[i].state, info[j].state);
				info[i].wait = max(info[i].wait, info[j].wait);
			}

			if (j > i + 1) {
				memcpy(info + i + 1, info + j, (totcnt - j) * sizeof(threadinfo_t));
				totcnt -= j - i - 1;
			}
		}
	}
	qsort(info, totcnt, sizeof(threadinfo_t), cmp);

	for (i = 0; i < totcnt; i++) {
		if (info[i].state == 0)
			runcnt++;
		else
			waitcnt++;
	}

	/* Move cursor to beginning */
	printf("\033[0;0f");
	printf("\033[K");
	if (threads_mode)
		printf("Threads: ");
	else
		printf("Tasks:    ");

	printf("%d total, running: %d, sleeping: %d\n", totcnt, runcnt, waitcnt);

	if (cmd)
		printf("Unknown command: %c\n", cmd);
	else
		printf("\033[K\n");

	/* Set header style */
	printf("\033[0;30;47m");
	printf("%5s %5s %2s %5s %5s %5s %7s %8s %-30s\n", "PID", "PPID", "PR", "STATE", "%CPU", "WAIT", "TIME", "VMEM", "CMD");

	/* Reset style */
	printf("\033[0m");
	for (i = 0; i < totcnt; i++) {

		psh_convert(SI, info[i].wait, -6, 1, buff);
		/* Print running process in bold */
		if (!info[i].state)
			printf("\033[1m");

		/* cpuTime is in usces */
		m = info[i].cpuTime / (60 * 1000000);
		s = info[i].cpuTime / 1000000 - 60 * m;
		hs = info[i].cpuTime / 10000 - 60 * 100 * m - 100 * s;
		printf("%5u %5u %2d %5s %3u.%u %4ss %3u:%02u.%02u ", (threads_mode) ? info[i].tid : info[i].pid,
			info[i].ppid, info[i].priority, (info[i].state) ? "sleep" : "ready",
			info[i].load / 10, info[i].load % 10, buff, m, s, hs);

		psh_convert(BP, info[i].vmem, 0, 1, buff);
		printf("%6s ", buff);
		printf("%-30s", info[i].name);

		printf("\033[0m");
		lines++;
		if (lines == w.ws_row)
			break;
		putchar('\n');
	}

	/* Clear pending lines */
	while (lines < prevlines) {
		printf("\033[K");
		prevlines--;
		if (lines != prevlines)
			putchar('\n');
	}
	prevlines = lines;
}


int psh_top(char *arg)
{
	int err = 0;
	int cmd = 0;
	unsigned int totcnt, n = 32;
	int itermode = 1;
	threadinfo_t *info, *info_re, *prev_info;
	unsigned int len;
	unsigned int delay = 3;
	unsigned int niter = 0;
	int ret = EOK;
	time_t prev_time;
	struct timespec ts;


	threads_mode = 0;
	sortdir = DESC;
	cmp = top_cmpcpu;
	prev_time = 0;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) != 0) {
		w.ws_row = 25;
		w.ws_col = 80;
	}

	/* Parse arguments */
	while ((arg = psh_nextString(arg, &len)) && len) {
		if (!strcmp(arg, "-H")) {
			threads_mode = 1;
		} else if (!strcmp(arg, "-d")) {
			arg += len + 1;
			if ((arg = psh_nextString(arg, &len)) && len) {
				delay = strtoul(arg, NULL, 10);
				if (delay == 0.0) {
					printf("top: '%s' requires int greater than 0\n", arg);
					return -1;
				}
			} else {
				printf("top: '%s' requires argument\n", arg);
				return -1;
			}
		} else if (!strcmp(arg, "-n")) {
			arg += len + 1;
			if ((arg = psh_nextString(arg, &len)) && len) {
				niter = strtoul(arg, NULL, 10);
				if (niter == 0) {
					printf("top: '%s' requires integer greater than 0\n", arg);
					return -1;
				}
				itermode = 1;
			} else {
				printf("top: '%s' requires argument\n", arg);
				return -1;
			}
		} else if (!strcmp(arg, "-h")) {
			top_printHelp();
			return 0;
		} else {
			printf("top: unknown option '%s'\n", arg);
			return EOK;
		}

		arg += len + 1;
	}

	if ((info = malloc(n * sizeof(threadinfo_t))) == NULL) {
		printf("top: out of memory\n");
		return -ENOMEM;
	}

	if ((prev_info = malloc(n * sizeof(threadinfo_t))) == NULL) {
		printf("top: out of memory\n");
		free(info);
		return -ENOMEM;
	}

	/* To refresh all tasks at once, flush stdout on fflush only */
	setvbuf(stdout, NULL,_IOFBF, 0);
	top_termSwitchMode(0);

	/* Clear terminal */
	printf("\033[2J");

	/* Disable cursor */
	printf("\033[?25l");

	while (1) {
		time_t delta, now;

		clock_gettime(CLOCK_MONOTONIC, &ts);
		/* Reallocate buffers if number of threads exceeds n */
		while ((totcnt = threadsinfo(n, info)) >= n) {
			n *= 2;
			if ((info_re = realloc(info, n * sizeof(threadinfo_t))) == NULL) {
				printf("ps: out of memory\n");
				ret = -ENOMEM;
				goto restore;
			}
			info = info_re;
			if ((info_re = realloc(prev_info, n * sizeof(threadinfo_t))) == NULL) {
				printf("ps: out of memory\n");
				ret = -ENOMEM;
				goto restore;
			}
			prev_info = info_re;
		}

		now = ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
		delta = now - prev_time;
		prev_time = now;

		top_refresh(err, info, prev_info, totcnt, delta);
		fflush(stdout);
		err = 0;

		if (itermode) {
			niter--;
			if (niter <= 0)
				break;
		}

		cmd = top_cmdwait(delay);
		switch (cmd) {
			case '\n':
			case ' ':
			case 0:
				continue;
			case 'q':
				goto restore;
			case 'H':
				threads_mode = !threads_mode;
				break;
			case 'N':
				cmp = top_cmppid;
				break;
			case 'P':
				cmp = top_cmpcpu;
				break;
			case 'M':
				cmp = top_cmpmem;
				break;
			case 'T':
				cmp =  top_cmptime;
				break;
			case 'R':
				sortdir *= -1;
				break;
			default:
				err = cmd;
		}
	}

restore:
	free(info);
	free(prev_info);
	printf("\033[?25h");
	top_termSwitchMode(1);
	setvbuf(stdout, NULL, _IOLBF, 0);

	return ret;
}
