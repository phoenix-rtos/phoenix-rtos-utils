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
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>

#include "../psh.h"


void psh_touchinfo(void)
{
	printf("changes file timestamp");
}


int psh_touch(int argc, char **argv)
{
	FILE *file;
	int i;
	struct timeval times;

	if (argc < 2) {
		fprintf(stderr, "usage: %s <file path>...\n", argv[0]);
		return -EINVAL;
	}

	for (i = 1; i < argc; i++) {
		file = fopen(argv[i], "r");
		if (file == NULL) {
			if (errno == ENOENT) {
				/* file does not exist -> create it */
				file = fopen(argv[i], "w");
				if (file != NULL) {
					fclose(file);
					continue;
				}
			}
		}
		else {
			/* file exists -> update timestamps */
			fclose(file);
			times.tv_sec = time(NULL);
			times.tv_usec = 0;
			if (utimes(argv[i], &times) == 0)
				continue;
		}
		fprintf(stderr, "psh: failed to touch %s\n", argv[i]);
	}

	return EOK;
}


void __attribute__((constructor)) touch_registerapp(void)
{
	static psh_appentry_t app = {.name = "touch", .run = psh_touch, .info = psh_touchinfo};
	psh_registerapp(&app);
}
