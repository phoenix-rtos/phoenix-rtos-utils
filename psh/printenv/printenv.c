/*
 * Phoenix-RTOS
 *
 * print environment variables
 *
 * Copyright 2023 Phoenix Systems
 * Author: Gerard Swiderski
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
#include <unistd.h>

#include <stdint.h>

#include "../psh.h"


static void psh_printEnvInfo(void)
{
	printf("print all or part of environment");
}


static int psh_printEnv(int argc, char **argv)
{
	extern char **environ;
	char **env = environ;

	if (env == NULL) {
		return EXIT_FAILURE;
	}

	if (argc == 1) {
		/* Print all environment */
		while (*env != NULL) {
			printf("%s\n", *(env++));
		}
		return EXIT_SUCCESS;
	}

	if (*argv[1] == '-') {
		fprintf(stderr, "psh: %s: %s unknown option\n", argv[0], argv[1]);
		fprintf(stderr, "usage: %s name[=value] ...]\n", argv[0]);
		return EXIT_FAILURE;
	}

	while (argc > 1) {
		/* Print part of environment */
		char *value = getenv(*(++argv));
		if (value != NULL) {
			printf("%s\n", value);
		}
		argc--;
	}

	return EXIT_SUCCESS;
}


void __attribute__((constructor)) printenv_registerapp(void)
{
	static psh_appentry_t app_printenv = { .name = "printenv", .run = psh_printEnv, .info = psh_printEnvInfo };
	psh_registerapp(&app_printenv);
}
