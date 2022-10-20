/*
 * Phoenix-RTOS
 *
 * hm - Health Monitor
 *
 * Copyright 2021 Phoenix Systems
 * Author: Aleksander Kaminski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/rb.h>
#include <errno.h>
#include <sys/threads.h>

#include "../psh.h"

#define ARG_SEPARATOR '@'

typedef struct {
	rbnode_t node;
	char *path;
	char **argv;
	pid_t pid;
} proc_t;


static struct {
	rbtree_t ptree;
} hm_common;


static int psh_hm_ptreeCompare(rbnode_t *n1, rbnode_t *n2)
{
	proc_t *p1 = lib_treeof(proc_t, node, n1);
	proc_t *p2 = lib_treeof(proc_t, node, n2);

	if (p1->pid < p2->pid)
		return -1;
	else if (p1->pid > p2->pid)
		return 1;

	return 0;
}


static void psh_hm_help(void)
{
	printf("usage: hm progname1[" "%c" "argv[0]" "%c" "argv[1]" "%c" "...argv[n]] [progname2...]\n",
		ARG_SEPARATOR, ARG_SEPARATOR, ARG_SEPARATOR);
}


static int psh_hm_spawn(proc_t *p)
{
	pid_t pid;

	pid = spawnSyspage(NULL, NULL, p->path, p->argv);
	if (pid < 0)
		return pid;

	p->pid = pid;

	return EOK;
}


static int psh_hm_argPrepare(const char *path, proc_t *p)
{
	int i, argc = 0;
	const char *t;
	static const char separator[2] = { ARG_SEPARATOR, '\0' };
	char **argv = NULL, *argstr;

	for (t = path; *t != '\0'; ++t) {
		if (*t == ARG_SEPARATOR)
			++argc;
	}

	argstr = strdup(path);
	if (argstr == NULL)
		return -ENOMEM;

	argv = calloc(argc + 2, sizeof(char *));
	if (argv == NULL) {
		free(argstr);
		return -ENOMEM;
	}

	p->path = strtok(argstr, separator);
	if (argc == 0) {
		argv[0] = p->path;
	}
	else {
		for (i = 0; i < argc; ++i) {
			argv[i] = strtok(NULL, separator);
			if (argv[i] == NULL) {
				free(argstr);
				free(argv);
				return -EINVAL;
			}
		}
	}

	p->argv = argv;

	return EOK;
}


void psh_hminfo(void)
{
	printf("health monitor, spawns apps and keeps them alive");
}


int psh_hm(int argc, char *argv[])
{
	int i, err, status, progs = 0;
	proc_t *p, t;
	pid_t pid;

	if (argc <= 1) {
		psh_hm_help();
		return EXIT_FAILURE;
	}

	lib_rbInit(&hm_common.ptree, psh_hm_ptreeCompare, NULL);

	for (i = 1; i < argc; ++i) {
		p = malloc(sizeof(*p));
		if (p == NULL) {
			fprintf(stderr, "hm: Out of memory\n");
			return EXIT_FAILURE;
		}

		err = psh_hm_argPrepare(argv[i], p);
		if (err < 0) {
			free(p);
			if (err == -ENOMEM) {
				fprintf(stderr, "hm: Out of memory\n");
				return EXIT_FAILURE;
			}
			else {
				fprintf(stderr, "hm: Failed to parse %s\n", argv[i]);
				continue;
			}
		}

		pid = psh_hm_spawn(p);
		if (pid < 0) {
			fprintf(stderr, "hm: Failed to spawn %s (%s)\n", p->argv[0], strerror(-pid));
			free(p->path);
			free(p->argv);
			free(p);
			continue;
		}

		lib_rbInsert(&hm_common.ptree, &p->node);
		printf("hm: Spawned %s successfully\n", p->argv[0]);
		++progs;
	}

	while (progs != 0) {
		pid = wait(&status);
		if (pid < 0)
			continue;

		t.pid = pid;
		p = lib_treeof(proc_t, node, lib_rbFind(&hm_common.ptree, &t.node));
		if (p == NULL) {
			fprintf(stderr, "hm: Child died, but it's not mine (pid %d). Ignoring.\n", pid);
			continue;
		}
		pid = psh_hm_spawn(p);
		if (pid < 0)
			fprintf(stderr, "hm: Failed to respawn %s (%s)\n", p->argv[0], strerror(-pid));
	}

	fprintf(stderr, "hm: No process to guard, exiting\n");

	return EXIT_SUCCESS;
}


void __attribute__((constructor)) hm_registerapp(void)
{
	static psh_appentry_t app = { .name = "hm", .run = psh_hm, .info = psh_hminfo };
	psh_registerapp(&app);
}
