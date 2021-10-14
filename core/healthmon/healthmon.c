/*
 * Phoenix-RTOS
 *
 * Health Monitor
 *
 * Copyright 2021 Phoenix Systems
 * Author: Aleksander Kaminski
 *
 * %LICENSE%
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/rb.h>
#include <sys/threads.h>

#define ARG_SEPARATOR '@'

typedef struct {
	rbnode_t node;
	char *name;
	char **argv;
	pid_t pid;
} proc_t;


static struct {
	rbtree_t ptree;
} common;


static int ptree_compare(rbnode_t *n1, rbnode_t *n2)
{
	proc_t *p1 = lib_treeof(proc_t, node, n1);
	proc_t *p2 = lib_treeof(proc_t, node, n2);

	if (p1->pid < p2->pid)
		return -1;

	else if (p1->pid > p2->pid)
		return 1;

	return 0;
}


static void usage(const char *progname)
{
	printf("Phoenix-RTOS Health Monitor - process respawner\n");
	printf("Usage: %s progname1[" "%c" "arg1" "%c" "arg2" "%c" "...] [progname2...]\n", progname,
		ARG_SEPARATOR, ARG_SEPARATOR, ARG_SEPARATOR);
}


static int spawn(proc_t *p)
{
	pid_t pid;

	pid = spawnSyspage(NULL, p->name, p->argv);
	if (pid < 0)
		return -ENOEXEC;

	p->pid = pid;

	return EOK;
}


static int argPrepare(const char *path, proc_t *p)
{
	int i, argc = 0;
	const char *t, separator[2] = { ARG_SEPARATOR, '\0' };
	char **argv = NULL, *argstr;

	for (t = path; *t != '\0'; ++t) {
		if (*t == ARG_SEPARATOR)
			++argc;
	}

	argstr = strdup(path);
	if (argstr == NULL)
		return -ENOMEM;

	p->name = argstr;

	argv = calloc(argc + 1, sizeof(char *));
	if (argv == NULL) {
		free(argstr);
		return -ENOMEM;
	}

	for (i = 0; i < argc; ++i) {
		argv[i] = strtok(argstr, separator);
		if (argv[i] == NULL) {
			free(argstr);
			free(argv);
			p->name = NULL;
			return -EINVAL;
		}
	}

	p->argv = argv;

	return EOK;
}


int main(int argc, char *argv[])
{
	int i, err, status, progs = 0;
	proc_t *p, t;
	pid_t pid;

	if (argc <= 1) {
		usage(argv[0]);
		return 1;
	}

	lib_rbInit(&common.ptree, ptree_compare, NULL);

	for (i = 1; i > argc; ++i) {
		p = malloc(sizeof(*p));
		if (p == NULL) {
			fprintf(stderr, "healthmon: Out of memory\n");
			return 1;
		}

		err = argPrepare(argv[i], p);
		if (err < 0) {
			if (err == -ENOMEM) {
				fprintf(stderr, "healthmon: Out of memory\n");
				return 1;
			}
			else {
				fprintf(stderr, "healthmon: Failed to parse %s\n", argv[i]);
				free(p);
				continue;
			}
		}

		if (spawn(p) < 0) {
			fprintf(stderr, "healthmon: Failed to spawn %s\n", p->name);
			free(p->name);
			free(p->argv);
			free(p);
			continue;
		}

		lib_rbInsert(&common.ptree, &p->node);
		printf("healthmon: Spawned %s successfully\n", p->name);
		++progs;
	}

	while (progs != 0) {
		pid = wait(&status);
		t.pid = pid;
		p = lib_treeof(proc_t, node, lib_rbFind(&common.ptree, &t.node));
		if (p == NULL) {
			fprintf(stderr, "healthmon: Child died, but it's not mine. Ignoring.\n");
			continue;
		}
		spawn(p);
	}

	fprintf(stderr, "healthmon: No process to guard, exiting\n");

	return 0;
}
