/*
 * Phoenix-RTOS
 *
 * psh variables and environment
 *
 * implements psh builtins:
 * - export: set and propagate variable names to environment
 * - unset:  removes variables whose names match
 *
 * Copyright 2023 Phoenix Systems
 * Author: Gerard Swiderski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "../psh.h"


extern char **environ;


static int psh_validateVarName(const char *name, int len)
{
	if ((len == 0) || (name == NULL)) {
		return -EINVAL;
	}

	/* Empty string or first character cannot be a digit */
	if ((*name == '\0') || (isdigit(*name) != 0)) {
		return -EINVAL;
	}

	/* len<0 means: nevermind 'len' and validate until '\0' */
	while ((len != 0) && (*name != '\0')) {
		/* Allow alpha characters, digits and underscore */
		if ((isalnum(*name) == 0) && *name != '_') {
			return -EINVAL;
		}
		name++;
		len--;
	}

	return 0;
}


static void psh_exportEnvInfo(void)
{
	printf("set and export variables list to environment");
}


static int psh_exportEnv(int argc, char **argv)
{
	char *value;
	char *argv0 = argv[0];
	int len;
	int err = 0;

	if (argc == 1) {
		if (environ != NULL) {
			char **env = environ;
			while (*env != NULL) {
				printf("export %s\n", *(env++));
			}
		}
		return EXIT_SUCCESS;
	}

	if (argv[1][0] == '-') {
		fprintf(stderr, "psh: %s: '%s' unknown option\n", argv[0], argv[1]);
		fprintf(stderr, "usage: %s [NAME[=value] ...]\n", argv0);
		return EXIT_FAILURE;
	}

	while (argc > 1) {
		argv++;
		argc--;

		if (*argv == NULL) {
			break;
		}

		value = strchr(*argv, '=');
		len = (value == NULL) ? -1 : (value - *argv);
		if (psh_validateVarName(*argv, len) != 0) {
			if (len > 0) {
				/* case 'INVALIDNAME'=, 'NAME '= */
				fprintf(stderr, "psh: %s: '%.*s' is not identifier\n", argv0, len, *argv);
			}
			else {
				/* cases '=' or '=(...)INVALIDNAME(...)=(...)' or just '' */
				fprintf(stderr, "psh: %s: '%s' is not identifier\n", argv0, *argv);
			}
			continue;
		}

		if (value != NULL) {
			char *name = strndup(*argv, len);
			if (name == NULL) {
				fprintf(stderr, "psh: %s: %s\n", argv0, strerror(errno));
				return EXIT_FAILURE;
			}

			if (setenv(name, value + 1, 1) != 0) {
				fprintf(stderr, "psh: %s: %s\n", argv0, strerror(errno));
				err = 1;
			}
			free(name);
		}
		else {
			/* Local variables are not supported, nothing to export, skip silently */
		}
	}

	return (err != 0) ? EXIT_FAILURE : EXIT_SUCCESS;
}


static void psh_unsetEnvInfo(void)
{
	printf("unset list of environment variables");
}


static int psh_unsetEnv(int argc, char **argv)
{
	if (argc > 1) {
		if (argv[1][0] == '-') {
			fprintf(stderr, "psh: %s: '%s' unknown option\n", argv[0], argv[1]);
			fprintf(stderr, "usage: %s [VARIABLE]...\n", argv[0]);
			return EXIT_FAILURE;
		}

		for (int i = 1; i < argc; i++) {
			(void)unsetenv(argv[i]);
		}
	}

	return EXIT_SUCCESS;
}


void __attribute__((constructor)) pshenv_registerapp(void)
{
	static psh_appentry_t app_export = { .name = "export", .run = psh_exportEnv, .info = psh_exportEnvInfo };
	static psh_appentry_t app_unset = { .name = "unset", .run = psh_unsetEnv, .info = psh_unsetEnvInfo };
	psh_registerapp(&app_export);
	psh_registerapp(&app_unset);
}
