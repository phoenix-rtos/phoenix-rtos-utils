/*
 * Phoenix-RTOS
 *
 * pwd - prints the name of current working directory
 *
 * Copyright 2022 Phoenix Systems
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
#include <unistd.h>

#include "../psh.h"


static void psh_pwd_info(void)
{
	printf("prints the name of current working directory");
}


static int psh_pwd(int argc, char **argv)
{
	char *cwd = getcwd(NULL, 0);

	if (cwd != NULL) {
		puts(cwd);
		free(cwd);
		return EXIT_SUCCESS;
	}

	fprintf(stderr, "pwd: Error: %s\n", strerror(errno));

	return EXIT_FAILURE;
}


void __attribute__((constructor)) pwd_registerapp(void)
{
	static psh_appentry_t app = { .name = "pwd", .run = psh_pwd, .info = psh_pwd_info };
	psh_registerapp(&app);
}
