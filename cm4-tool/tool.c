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
#include <termios.h>
#include <sys/msg.h>
#include <sys/threads.h>
#include <imxrt117x-cm4.h>


static const char blinky[] = {
#include "blinky.hex"
};


static volatile int done = 0;


void rx_thread(void *arg)
{
	char byte;
	FILE *f = arg;

	while (!done) {
		if (fread(&byte, 1, 1, f)) {
			printf("%c", byte);
			fflush(NULL);
		}
	}

	endthread();
}


void tx_thread(void *arg)
{
	char byte;
	FILE *f = arg;

	while ((byte = getchar()) != 27) {
		fwrite(&byte, 1, 1, f);
		fflush(f);
	}

	done = 1;
}


int main(int argc, char *argv[])
{
	msg_t msg;
	imxrt117xM4DevCtli_t *i = (void *)msg.i.raw;
	imxrt117xM4DevCtlo_t *o = (void *)msg.o.raw;
	int opt, usage = 0, file = 0, example = 0, terminal = 0, termno = -1;
	char *path = NULL;
	oid_t driver;
	FILE *f;
	void *stack;
	struct termios term;

	if (argc == 1)
		return 0;

	if (lookup("/dev/cpuM40", NULL, &driver) < 0) {
		fprintf(stderr, "imxrt117x-cm4 driver not found!\n");

		return -1;
	}

	while ((opt = getopt(argc, argv, "f:t:e")) >= 0) {
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

		case 't':
			terminal = 1;
			termno = (int)strtol(optarg, NULL, 10);
			break;

		default:
			usage = 1;
			break;
		}
	}

	if (usage || (file && example) || (!file && !example && !terminal) ||
			(terminal && (termno < 0 || termno > 4))) {
		fprintf(stderr, "Shell tool for imxrt117x-cm4 driver. Usage:\n");
		fprintf(stderr, "%s [-t term] <-f file | -e>\n", argv[0]);
		fprintf(stderr, "\t-f Run binary file <file>]\n");
		fprintf(stderr, "\t-e Run example (blinky)\n");
		fprintf(stderr, "\t-t Run terminal <term>. Exit with ESC\n");

		free(path);

		return 1;
	}

	if (file || example) {
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
	}

	free(path);

	if (terminal) {
		tcgetattr(0, &term);
		cfmakeraw(&term);
		tcsetattr(0, TCSANOW, &term);

		if ((path = strdup("/dev/cpuM40")) == NULL) {
			fprintf(stderr, "Out of memory\n");
			return -1;
		}

		if ((stack = malloc(1024)) == NULL) {
			fprintf(stderr, "Out of memory\n");
			return -1;
		}

		path[strlen(path) - 1] += termno;
		if ((f = fopen(path, "r+")) == NULL) {
			fprintf(stderr, "Could not open %s\n", path);
			free(path);
			return -1;
		}
		free(path);

		beginthread(rx_thread, 4, stack, 1024, f);
		tx_thread(f);

		threadJoin(-1, 0);
		free(stack);

		fprintf(stderr, "Terminal done\n");
	}

	return 0;
}
