/*
 * Phoenix-RTOS
 *
 * kill - send a signal to a process
 *
 * Copyright 2017, 2018, 2020, 2021, 2024 Phoenix Systems
 * Author: Pawel Pisarczyk, Jan Sikorski, Maciej Purski,
 * Lukasz Kosinski, Mateusz Niewiadomski, Gerard Swiderski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include "../psh.h"


static void psh_killInfo(void)
{
	printf("sends a signal to a process");
}


static void killUsage(void)
{
	puts("Usage: kill [-s signal | -signal] <pid [...]>");
}


static int signalByName(const char *name)
{
	static const struct {
		const char *name;
		int signal;
	} sigNames[] = {
		/* clang-format on */

		{ "HUP", SIGHUP }, { "INT", SIGINT }, { "QUIT", SIGQUIT }, { "ILL", SIGILL },
		{ "TRAP", SIGTRAP }, { "ABRT", SIGABRT }, { "EMT", SIGEMT }, { "FPE", SIGFPE },
		{ "KILL", SIGKILL }, { "BUS", SIGBUS }, { "SEGV", SIGSEGV }, { "SYS", SIGSYS },
		{ "PIPE", SIGPIPE }, { "ALRM", SIGALRM }, { "TERM", SIGTERM }, { "USR1", SIGUSR1 },
		{ "USR2", SIGUSR2 }, { "CHLD", SIGCHLD }, { "WINCH", SIGWINCH }, { "URG", SIGURG },
		{ "IO", SIGIO }, { "STOP", SIGSTOP }, { "TSTP", SIGTSTP }, { "CONT", SIGCONT },
		{ "TTIN", SIGTTIN }, { "TTOU", SIGTTOU }, { "VTALRM", SIGVTALRM }, { "PROF", SIGPROF },
		{ "XCPU", SIGXCPU }, { "XFSZ", SIGXFSZ }, { "INFO", SIGINFO }

		/* clang-format off */
	};

	if ((name[0] == 'S') && (name[1] == 'I') && (name[2] == 'G')) {
		name += 3;
	}

	for (size_t i = 0; i < sizeof(sigNames) / sizeof(sigNames[0]); ++i) {
		if (strcmp(name, sigNames[i].name) == 0) {
			return sigNames[i].signal;
		}
	}

	return -1;
}


static int psh_killMain(int argc, char **argv)
{
	char *end;
	int sigNo = SIGTERM;
	int argn = 1;

	if (argc <= argn) {
		killUsage();
		return EXIT_FAILURE;
	}

	if (argv[1][0] == '-') {
		char *sigArg = argv[argn++] + 1;
		if (argv[1][1] == 's') {
			if ((argv[1][2] != '\0')) {
				killUsage();
				return EXIT_FAILURE;
			}
			sigArg = argv[argn++];
		}
		if ((argc <= argn) || (argv[1][1] == '-') || (argv[1][1] == '\0')) {
			killUsage();
			return EXIT_FAILURE;
		}

		unsigned long int signalValue = strtoul(sigArg, &end, 10);

		if (*end == '\0') {
			sigNo = (int)signalValue;
		}
		else {
			sigNo = signalByName(sigArg);
			if (sigNo < 0) {
				fprintf(stderr, "kill: invalid signal name: %s\n", sigArg);
				return EXIT_FAILURE;
			}
		}
	}

	for (; argn < argc; ++argn) {
		errno = 0;
		pid_t pid = (pid_t)strtol(argv[argn], &end, 10);

		if ((argv[argn][0] == '-') || (*end != '\0') || ((pid == 0) && (errno == EINVAL))) {
			fprintf(stderr, "kill: invalid process id: %s\n", argv[argn]);
			return EXIT_FAILURE;
		}

		if (kill(pid, sigNo) != 0) {
			fprintf(stderr, "kill: failed to send signal to process %d\n", pid);
			return EXIT_FAILURE;
		}
	}

	return EXIT_SUCCESS;
}


static void __attribute__((constructor)) kill_registerapp(void)
{
	static psh_appentry_t app = { .name = "kill", .run = psh_killMain, .info = psh_killInfo };
	psh_registerapp(&app);
}
