/*
 * Phoenix-RTOS
 *
 * i.MX RT117x M4 cpu core tool
 *
 * Copyright 2021 Phoenix Systems
 * Author: Aleksander Kaminski
 *
 * %LICENSE%
 */

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <sys/msg.h>
#include <imxrt117x-cm4.h>


static const char blinky[] = {
#include "blinky.h"
};


int main(int argc, char *argv[])
{
	msg_t msg;
	imxrt117xM4DevCtli_t *i = (void *)msg.i.raw;
	imxrt117xM4DevCtlo_t *o = (void *)msg.o.raw;
	int opt, usage = 0, file = 0, example = 0;
	char *path = NULL;
	oid_t driver;

	if (argc == 1)
		return 0;

	if (lookup("/dev/cpuM40", NULL, &driver) < 0) {
		fprintf(stderr, "imxrt117x-cm4 driver not found!\n");

		return -1;
	}

	while ((opt = getopt(argc, argv, "f:e")) >= 0) {
		switch (opt) {
		case 'f':
			file = 1;
			if ((path = strdup(optarg)) == NULL) {
				fprintf(stderr, "Out of memory\n");
				return -1;
			}
			break;

		case 'e':
			example = 1;
			break;

		default:
			usage = 1;
			break;
		}
	}

	if (usage || (file && example) || (!file && !example)) {
		fprintf(stderr, "Shell tool for imxrt117x-cm4 driver. Usage:\n");
		fprintf(stderr, "%s [-f file | -e]\n", argv[0]);
		fprintf(stderr, "\t-f Run binary file [file]\n");
		fprintf(stderr, "\t-e Run example (blinky)\n");

		free(path);

		return 1;
	}

	msg.type = mtDevCtl;
	msg.o.data = NULL;
	msg.o.size = 0;

	if (file) {
		i->type = m4_loadFile;
		msg.i.data = path;
		msg.i.size = strlen(path);
	}
	else {
		i->type = m4_loadBuff;
		msg.i.data = (void *)blinky;
		msg.i.size = sizeof(blinky);
	}


	if (msgSend(driver.port, &msg) < 0) {
		fprintf(stderr, "msgSend failed\n");
		free(path);

		return -1;
	}

	if (o->err < 0) {
		fprintf(stderr, "imxrt117x-cm4 driver failed to run the binary (err %d)\n", o->err);
		free(path);

		return -1;
	}

	fprintf(stderr, "Loading successful, starting the core\n");

	i->type = m4_runCore;
	msg.i.data = NULL;
	msg.i.size = 0;

	if (msgSend(driver.port, &msg) < 0) {
		fprintf(stderr, "msgSend failed\n");
		free(path);

		return -1;
	}

	if (o->err < 0) {
		fprintf(stderr, "imxrt117x-cm4 driver failed to start the core (err %d)\n", o->err);
		free(path);

		return -1;
	}

	fprintf(stderr, "Done\n");

	free(path);

	return 0;
}
