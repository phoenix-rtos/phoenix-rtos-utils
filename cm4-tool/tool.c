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
#include <imxrt-multi.h>


static const char blinky[] = {
#include "blinky.hex"
};


static volatile int done = 0;
static char buf[256];


void rx_thread(void *arg)
{
	size_t n;
	FILE *f = arg;

	while (!done) {
		n = fread(buf, 1, sizeof(buf), f);
		if (n > 0) {
			fwrite(buf, 1, n, stdout);
			fflush(stdout);
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
	multi_i_t *i = (void *)msg.i.raw;
	multi_o_t *o = (void *)msg.o.raw;
	int opt, usage = 0, file = 0, example = 0, start = 0, terminal = 0, termno = -1;
	unsigned int offset = 0;
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

	while ((opt = getopt(argc, argv, "f:t:eso:")) >= 0) {
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

		case 's':
			start = 1;
			break;

		case 't':
			terminal = 1;
			termno = (int)strtol(optarg, NULL, 0);
			break;

		case 'o':
			offset = (unsigned int)strtol(optarg, NULL, 0);
			break;

		default:
			usage = 1;
			break;
		}
	}

	if (usage || (file + example + start) > 1 || (!file && !example && !start && !terminal) ||
			(terminal && (termno < 0 || termno > 4))) {
		fprintf(stderr, "Shell tool for imxrt117x-cm4 driver. Usage:\n");
		fprintf(stderr, "%s [-t term] [-o addr] <-f file | -e | -s>\n", argv[0]);
		fprintf(stderr, "\t-f Run binary file <file>]\n");
		fprintf(stderr, "\t-e Run example (blinky)\n");
		fprintf(stderr, "\t-s Start core only\n");
		fprintf(stderr, "\t-t Run terminal <term>. Exit with ESC\n");
		fprintf(stderr, "\t-o Set vectors table offset to <addr> (default 0)\n");

		free(path);

		return 1;
	}

	if (file || example) {
		msg.type = mtDevCtl;
		msg.o.data = NULL;
		msg.o.size = 0;

		i->id = driver.id;

		if (file) {
			i->cm4_type = CM4_LOAD_FILE;
			msg.i.data = path;
			msg.i.size = strlen(path);
		}
		else {
			i->cm4_type = CM4_LOAD_BUFF;
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

		fprintf(stderr, "Loading successful\n");
	}

	if (file || example || start) {
		fprintf(stderr, "Starting the core\n");

		msg.type = mtDevCtl;
		msg.o.data = NULL;
		msg.o.size = 0;
		i->cm4_type = CM4_RUN_CORE;
		i->id = driver.id;
		msg.i.data = (void *)&offset;
		msg.i.size = sizeof(offset);

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
