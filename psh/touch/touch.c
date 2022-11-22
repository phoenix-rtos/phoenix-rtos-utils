/*
 * Phoenix-RTOS
 *
 * touch - changes file timestamp
 *
 * Copyright 2017, 2018, 2020, 2021, 2022 Phoenix Systems
 * Author: Pawel Pisarczyk, Jan Sikorski, Maciej Purski, Lukasz Kosinski, Mateusz Niewiadomski, Damian Loewnau
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
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
	int i, err;

	err = EXIT_SUCCESS;

	if (argc < 2) {
		fprintf(stderr, "usage: %s <file path>...\n", argv[0]);
		return EXIT_FAILURE;
	}

	for (i = 1; i < argc; i++) {
		/* try to update timestamps */
		if (utimes(argv[i], NULL) != 0) {
			if (errno == ENOENT) {
				/* file does not exist -> create it */
				file = fopen(argv[i], "w");
				if (file != NULL) {
					fclose(file);
					continue;
				}
			}
			err = EXIT_FAILURE;
			fprintf(stderr, "psh: failed to touch %s\n", argv[i]);
		}
	}

	return err;
}


void __attribute__((constructor)) touch_registerapp(void)
{
	static psh_appentry_t app = { .name = "touch", .run = psh_touch, .info = psh_touchinfo };
	psh_registerapp(&app);
}
