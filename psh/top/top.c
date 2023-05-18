/*
 * Phoenix-RTOS
 *
 * top - prints threads and processes
 *
 * Copyright 2020, 2021 Phoenix Systems
 * Author: Maciej Purski, Lukasz Kosinski, Mateusz Niewiadomski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/minmax.h>
#include <sys/threads.h>
#include <sys/time.h>

#include "../psh.h"


static struct {
	int sortdir;
	int threads;
	struct winsize ws;
	int (*cmp)(const void *, const void *);
} psh_top_common;


static int psh_top_cmpcpu(const void *t1, const void *t2)
{
	return (((threadinfo_t *)t1)->load - ((threadinfo_t *)t2)->load) * psh_top_common.sortdir;
}


static int psh_top_cmppid(const void *t1, const void *t2)
{
	if (psh_top_common.threads)
		return ((int)((threadinfo_t *)t1)->tid - (int)((threadinfo_t *)t2)->tid) * psh_top_common.sortdir;
	else
		return ((int)((threadinfo_t *)t1)->pid - (int)((threadinfo_t *)t2)->pid) * psh_top_common.sortdir;
}


static int psh_top_cmptime(const void *t1, const void *t2)
{
	return ((int)((threadinfo_t *)t1)->cpuTime - (int)((threadinfo_t *)t2)->cpuTime) * psh_top_common.sortdir;
}


static int psh_top_cmpmem(const void *t1, const void *t2)
{
	return ((int)((threadinfo_t *)t1)->vmem - (int)((threadinfo_t *)t2)->vmem) * psh_top_common.sortdir;
}


static void psh_top_help(void)
{
	printf("Command line arguments:\n");
	printf("  -h:  prints help\n");
	printf("  -H:  starts with threads mode\n");
	printf("  -d:  sets refresh rate (integer greater than 0)\n");
	printf("  -n:  sets number of iterations (by default its infinity)\n\n");
	printf("Interactive commands:\n");
	printf("   <ENTER> or <SPACE>:  refresh\n");
	printf("   H:  toggle threads mode\n");
	printf("   q:  quit\n");
	printf("   P:  sort by CPU\n");
	printf("   M:  sort by MEM\n");
	printf("   T:  sort by TIME\n");
	printf("   N:  sort by PID\n");
	printf("   R:  reverse sorting\n");
}


/* Enables/disables canon mode and echo */
static void psh_top_switchmode(int canon)
{
	struct termios state;

	tcgetattr(STDIN_FILENO, &state);
	if (canon) {
		state.c_lflag |= ICANON;
		state.c_lflag |= ECHO;
	} else {
		state.c_lflag &= ~ICANON;
		state.c_lflag &= ~ECHO;
		state.c_cc[VMIN] = 1;
	}
	tcsetattr(STDIN_FILENO, TCSANOW, &state);
}


/* Interruptible wait for command */
static int psh_top_waitcmd(unsigned int secs)
{
	struct timeval left;
	fd_set fds;

	left.tv_sec = (time_t)secs;
	left.tv_usec = 0;
	fflush(stdout);
	FD_ZERO(&fds);
	FD_SET(STDIN_FILENO, &fds);
	select(STDIN_FILENO + 1, &fds, NULL, NULL, &left);

	if (FD_ISSET(STDIN_FILENO, &fds))
		return getchar();

	return EOK;
}


static void psh_top_refresh(char cmd, threadinfo_t *info, threadinfo_t *previnfo, unsigned int totcnt, time_t delta)
{
	static unsigned int prevlines = 0;
	static unsigned int prevcnt = 0;
	unsigned int i, j, m, s, hs, lines = 3, runcnt = 0, waitcnt = 0;
	char buff[8];

	/* Calculate load */
	for (i = 0; i < totcnt; i++) {
		threadinfo_t *p = NULL;
		for (j = 0; j < prevcnt; j++) {
			if (info[i].tid == previnfo[j].tid) {
				p = &previnfo[j];
				break;
			}
		}

		/* Prevent negative load if a new thread with the same tid has occured */
		if (p)
			info[i].load = (info[i].cpuTime > p->cpuTime) ? (info[i].cpuTime - p->cpuTime) * 1000 / delta : 0;
	}

	prevcnt = totcnt;
	memcpy(previnfo, info, totcnt * sizeof(threadinfo_t));

	if (!psh_top_common.threads) {
		qsort(info, totcnt, sizeof(threadinfo_t), psh_top_cmppid);
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
	qsort(info, totcnt, sizeof(threadinfo_t), psh_top_common.cmp);

	for (i = 0; i < totcnt; i++) {
		if (info[i].state == 0)
			runcnt++;
		else
			waitcnt++;
	}

	/* Move cursor to beginning */
	printf("\033[f");
	printf("\033[K");
	if (psh_top_common.threads)
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
	printf("%8s %8s %2s %5s %5s %7s %9s %8s %-20s\n", (psh_top_common.threads) ? "TID" : "PID", "PPID", "PR", "STATE", "%CPU", "WAIT", "TIME", "VMEM", "CMD");

	/* Reset style */
	printf("\033[0m");
	for (i = 0; i < totcnt; i++) {
		psh_prefix(10, info[i].wait, -6, 1, buff);

		/* Print running process in bold */
		if (!info[i].state)
			printf("\033[1m");

		/* cpuTime is in usces */
		m = info[i].cpuTime / (60 * 1000000);
		s = info[i].cpuTime / 1000000 - 60 * m;
		hs = info[i].cpuTime / 10000 - 60 * 100 * m - 100 * s;
		printf("%8u %8u %2d %5s %3u.%u %6ss %3u:%02u.%02u ", (psh_top_common.threads) ? info[i].tid : info[i].pid,
			info[i].ppid, info[i].priority, (info[i].state) ? "sleep" : "ready",
			info[i].load / 10, info[i].load % 10, buff, m, s, hs);

		psh_prefix(2, info[i].vmem, 0, 1, buff);
		printf("%8s ", buff);
		printf("%-20s\n", info[i].name);

		printf("\033[0m");

		if (++lines == psh_top_common.ws.ws_row)
			break;
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


static void psh_top_free(threadinfo_t *info, threadinfo_t *previnfo)
{
	free(info);
	free(previnfo);
	printf("\033[?25h");
	psh_top_switchmode(1);
	setvbuf(stdout, NULL, _IOLBF, 0);
}


void psh_topinfo(void)
{
	printf("top utility");
}


int psh_top(int argc, char **argv)
{
	int c, err = 0, cmd = 0, itermode = 1, run = 1, ret = EOK;
	unsigned int totcnt, n = 32, delay = 3, niter = 0;
	threadinfo_t *info, *rinfo, *previnfo;
	time_t prev_time = 0;
	struct timespec ts;
	char *end;

	psh_top_common.threads = 0;
	psh_top_common.sortdir = -1;
	psh_top_common.cmp = psh_top_cmpcpu;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &psh_top_common.ws) < 0) {
		psh_top_common.ws.ws_row = 25;
		psh_top_common.ws.ws_col = 80;
	}

	while ((c = getopt(argc, argv, "Hd:n:h")) != -1) {
		switch (c) {
		case 'H':
			psh_top_common.threads = 1;
			break;

		case 'd':
			delay = strtoul(optarg, &end, 10);
			if (*end != '\0' || !delay) {
				fprintf(stderr, "top: -d option requires integer greater than 0\n");
				return -EINVAL;
			}
			break;

		case 'n':
			niter = strtoul(optarg, &end, 10);
			if (*end != '\0' || !niter) {
				fprintf(stderr, "top: -n option requires integer greater than 0\n");
				return -EINVAL;
			}
			break;

		case 'h':
		default:
			psh_top_help();
			return EOK;
		}
	}

	if ((info = malloc(n * sizeof(threadinfo_t))) == NULL) {
		fprintf(stderr, "top: out of memory\n");
		return -ENOMEM;
	}

	if ((previnfo = malloc(n * sizeof(threadinfo_t))) == NULL) {
		fprintf(stderr, "top: out of memory\n");
		free(info);
		return -ENOMEM;
	}

	/* To refresh all tasks at once, flush stdout on fflush only */
	setvbuf(stdout, NULL,_IOFBF, 0);
	psh_top_switchmode(0);

	/* Clear terminal */
	printf("\033[2J");

	/* Disable cursor */
	printf("\033[?25l");

	while (!psh_common.sigint && !psh_common.sigquit && !psh_common.sigstop && run) {
		time_t delta, now;

		clock_gettime(CLOCK_MONOTONIC, &ts);
		/* Reallocate buffers if number of threads exceeds n */
		while ((totcnt = threadsinfo(n, info)) >= n) {
			n *= 2;
			if ((rinfo = realloc(info, n * sizeof(threadinfo_t))) == NULL) {
				fprintf(stderr, "ps: out of memory\n");
				psh_top_free(info, previnfo);
				return -ENOMEM;
			}
			info = rinfo;
			if ((rinfo = realloc(previnfo, n * sizeof(threadinfo_t))) == NULL) {
				fprintf(stderr, "ps: out of memory\n");
				psh_top_free(info, previnfo);
				return -ENOMEM;
			}
			previnfo = rinfo;
		}

		now = ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
		delta = now - prev_time;
		prev_time = now;

		psh_top_refresh(err, info, previnfo, totcnt, delta);
		fflush(stdout);
		err = 0;

		if (itermode) {
			niter--;
			if (niter <= 0)
				break;
		}

		cmd = psh_top_waitcmd(delay);
		switch (cmd) {
			case '\n':
			case ' ':
			case 0:
				continue;

			case 'q':
				run = 0;
				break;

			case 'H':
				psh_top_common.threads = !psh_top_common.threads;
				break;

			case 'N':
				psh_top_common.cmp = psh_top_cmppid;
				break;

			case 'P':
				psh_top_common.cmp = psh_top_cmpcpu;
				break;

			case 'M':
				psh_top_common.cmp = psh_top_cmpmem;
				break;

			case 'T':
				psh_top_common.cmp =  psh_top_cmptime;
				break;

			case 'R':
				psh_top_common.sortdir *= -1;
				break;

			default:
				err = cmd;
		}
	}
	psh_top_free(info, previnfo);

	return ret;
}


void __attribute__((constructor)) top_registerapp(void)
{
	static psh_appentry_t app = {.name = "top", .run = psh_top, .info = psh_topinfo};
	psh_registerapp(&app);
}
