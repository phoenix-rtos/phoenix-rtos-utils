/*
 * Phoenix-RTOS
 *
 * echo - display a line of text
 *
 * Copyright 2022 Phoenix Systems
 * Author: Aleksander Kaminski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../psh.h"


static void psh_echoinfo(void)
{
	printf("display a line of text");
}


static void psh_echo_help(const char *prog)
{
	printf("Usage: %s [options] [string]\n", prog);
	printf("  -n:  do not output the trailing newline\n");
	printf("  -h:  shows this help message\n");
	printf("\nAvailable variables:\n");
	printf("  $?:  Exit code of the previous command\n");
}


static size_t psh_echo_printVar(const char *var)
{
	size_t i;

	if (*var == '?') {
		/* ? is a special case - we eat only ?, even if no space is present */
		i = 1;
		printf("%d", psh_common.exitStatus);
	}
	else {
		/* Just eat non-existend variable name */
		for (i = 0; var[i] != ' ' && var[i] != '\0' && var[i] != '$'; ++i) {
			;
		}
	}

	return i; /* Eaten variable length */
}


static int psh_echo(int argc, char **argv)
{
	int c, i, argend = argc, newline = 1;
	size_t j;

	while ((c = getopt(argc, argv, "hn")) != -1) {
		switch (c) {
			case 'h':
				psh_echo_help(argv[0]);
				return EXIT_SUCCESS;

			case 'n':
				newline = 0;
				break;

			default:
				psh_echo_help(argv[0]);
				return EXIT_FAILURE;
		}
	}

	for (i = optind; i < argend; ++i) {
		if (i != optind) {
			putchar(' ');
		}

		for (j = 0; argv[i][j] != '\0'; ++j) {
			if (argv[i][j] == '$') {
				j += psh_echo_printVar(&argv[i][j + 1]);
			}
			else if (argv[i][j] != '"') { /* Primitive - just eat "" */
				putchar(argv[i][j]);
			}
		}
	}

	if (newline != 0) {
		putchar('\n');
	}
	fflush(stdout);

	return EOK;
}


void __attribute__((constructor)) echo_registerapp(void)
{
	static psh_appentry_t app = { .name = "echo", .run = psh_echo, .info = psh_echoinfo };
	psh_registerapp(&app);
}
