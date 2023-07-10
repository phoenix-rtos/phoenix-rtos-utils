/*
 * Phoenix-RTOS
 *
 * xd - conxdenate file(s) to standard output
 *
 * Copyright 2017, 2018, 2020-2022 Phoenix Systems
 * Author: Pawel Pisarczyk, Jan Sikorski, Lukasz Kosinski, Mateusz Niewiadomski, Aleksander Kaminski
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
#include <termios.h>

#include "../psh.h"

void psh_xdinfo(void)
{
	printf("read certain amount of characters from stdin and print it to stdout");
}

int psh_xd(int argc, char **argv)
{
	if(argc < 2) {
		return 1;
	}

	int size = atoi(argv[1]);
	char *buf = malloc(size);
	char format[32];
	struct termios state;

	tcgetattr(STDIN_FILENO, &state);

	state.c_lflag &= ~ECHO;

	tcsetattr(STDIN_FILENO, 0, &state);

	sprintf(format, "%%%ds", size - 1);

	scanf(format, buf);

	printf("%.*s\n", size, buf);

	free(buf);

	return 0;
}


void __attribute__((constructor)) xd_registerapp(void)
{
	static psh_appentry_t app = { .name = "xd", .run = psh_xd, .info = psh_xdinfo };
	psh_registerapp(&app);
}
