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
	char *path;
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
	printf("Usage: %s progname1[" "%c" "argv[0]" "%c" "argv[1]" "%c" "...argv[n]] [progname2...]\n", progname,
		ARG_SEPARATOR, ARG_SEPARATOR, ARG_SEPARATOR);
}


static int spawn(proc_t *p)
{
	pid_t pid;

	pid = spawnSyspage(NULL, p->path, p->argv);
	if (pid < 0)
		return pid;

	p->pid = pid;

	return EOK;
}


static int argPrepare(const char *path, proc_t *p)
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


int main(int argc, char *argv[])
{
	int i, err, status, progs = 0;
	proc_t *p, t;
	pid_t pid;

	if (argc <= 1) {
		usage(argv[0]);
		return EXIT_FAILURE;
	}

	lib_rbInit(&common.ptree, ptree_compare, NULL);

	for (i = 1; i < argc; ++i) {
		p = malloc(sizeof(*p));
		if (p == NULL) {
			fprintf(stderr, "healthmon: Out of memory\n");
			return EXIT_FAILURE;
		}

		err = argPrepare(argv[i], p);
		if (err < 0) {
			free(p);
			if (err == -ENOMEM) {
				fprintf(stderr, "healthmon: Out of memory\n");
				return EXIT_FAILURE;
			}
			else {
				fprintf(stderr, "healthmon: Failed to parse %s\n", argv[i]);
				continue;
			}
		}

		pid = spawn(p);
		if (pid < 0) {
			fprintf(stderr, "healthmon: Failed to spawn %s (%s)\n", p->path, strerror(-pid));
			free(p->path);
			free(p->argv);
			free(p);
			continue;
		}

		lib_rbInsert(&common.ptree, &p->node);
		printf("healthmon: Spawned %s successfully\n", p->path);
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
		pid = spawn(p);
		if (pid < 0)
			fprintf(stderr, "healthmon: Failed to respawn %s (%s)\n", p->path, strerror(-pid));
	}

	fprintf(stderr, "healthmon: No process to guard, exiting\n");

	return EXIT_SUCCESS;
}
