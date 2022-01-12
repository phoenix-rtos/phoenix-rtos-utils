/*
 * Phoenix-RTOS
 *
 * touch - changes file timestamp
 *
 * Copyright 2017, 2018, 2020, 2021 Phoenix Systems
 * Author: Pawel Pisarczyk, Jan Sikorski, Maciej Purski, Lukasz Kosinski, Mateusz Niewiadomski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <errno.h>
#include <stdio.h>

#include "../psh.h"


void psh_touchinfo(void)
{
	printf("changes file timestamp");
}


int psh_touch(int argc, char **argv)
{
	FILE *file;
	int i;

	if (argc < 2) {
		fprintf(stderr, "usage: %s <file path>...\n", argv[0]);
		return -EINVAL;
	}

	for (i = 1; i < argc; i++) {
		if ((file = fopen(argv[i], "w")) == NULL)
			fprintf(stderr, "touch: failed to open %s\n", argv[i]);
		else
			fclose(file);
	}

	return EOK;
}


void __attribute__((constructor)) touch_registerapp(void)
{
	static psh_appentry_t app = {.name = "touch", .run = psh_touch, .info = psh_touchinfo};
	psh_registerapp(&app);
}
