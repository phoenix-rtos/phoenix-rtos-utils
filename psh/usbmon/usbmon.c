/*
 * Phoenix-RTOS
 *
 * usbmon - control USB bus monitoring (PCAPng capture)
 *
 * Copyright 2026 Phoenix Systems
 * Author: Adam Greloch
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
#include <sys/msg.h>

#include <usbdriver.h>

#include "../psh.h"

#define LOOKUP_RETRIES 5


static void psh_usbmonInfo(void)
{
	printf("control USB bus monitoring");
}


static void psh_usbmonHelp(const char *prog)
{
	printf("Usage: %s start -o <pcapng_path> [-s <snaplen>]\n", prog);
	printf("       %s stop\n", prog);
	printf("  -o:  output file path (PCAPng format)\n");
	printf("  -s:  max payload bytes to capture per transfer (default: 256)\n");
}


static int usbmon_lookup(oid_t *oid)
{
	int i, ret;

	for (i = 0; i < LOOKUP_RETRIES; i++) {
		ret = lookup("/dev/usb", NULL, oid);
		if (ret == 0) {
			return 0;
		}
		usleep(100 * 1000);
	}

	fprintf(stderr, "usbmon: cannot find /dev/usb: %s\n", strerror(-ret));
	return ret;
}


static int usbmon_sendMsg(int action, const char *path, uint32_t snaplen)
{
	oid_t oid;
	msg_t msg = { 0 };
	usb_msg_t *umsg = (usb_msg_t *)msg.i.raw;
	int ret;

	ret = usbmon_lookup(&oid);
	if (ret < 0) {
		return EXIT_FAILURE;
	}

	msg.type = mtDevCtl;
	umsg->type = usb_msg_usbmon;
	umsg->usbmon.action = action;
	umsg->usbmon.snaplen = snaplen;

	if (path != NULL) {
		msg.i.data = (void *)path;
		msg.i.size = strlen(path) + 1;
	}

	ret = msgSend(oid.port, &msg);
	if (ret < 0) {
		fprintf(stderr, "usbmon: msgSend failed: %s\n", strerror(-ret));
		return EXIT_FAILURE;
	}

	if (msg.o.err < 0) {
		fprintf(stderr, "usbmon: %s\n", strerror(-msg.o.err));
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}


static int psh_usbmon(int argc, char **argv)
{
	const char *path = NULL;
	uint32_t snaplen = 0;
	int c, ret;

	if (argc < 2) {
		psh_usbmonHelp(argv[0]);
		return EXIT_FAILURE;
	}

	if (strcmp(argv[1], "start") == 0) {
		/* Parse options after "start" */
		optind = 1;
		while ((c = getopt(argc - 1, argv + 1, "o:s:")) != -1) {
			switch (c) {
				case 'o':
					path = optarg;
					break;
				case 's': {
					char *endptr;
					unsigned long val = strtoul(optarg, &endptr, 10);
					if (*endptr != '\0' || val > UINT32_MAX) {
						fprintf(stderr, "usbmon: invalid snaplen: %s\n", optarg);
						return EXIT_FAILURE;
					}
					snaplen = (uint32_t)val;
					break;
				}
				default:
					psh_usbmonHelp(argv[0]);
					return EXIT_FAILURE;
			}
		}

		if (path == NULL) {
			fprintf(stderr, "usbmon: -o <pcapng_path> is required\n");
			psh_usbmonHelp(argv[0]);
			return EXIT_FAILURE;
		}

		ret = usbmon_sendMsg(usb_usbmon_start, path, snaplen);
		if (ret == EXIT_SUCCESS) {
			printf("usbmon: capturing to %s", path);
			if (snaplen != 0) {
				printf(" (snaplen %u)", snaplen);
			}
			printf("\n");
		}
		return ret;
	}
	else if (strcmp(argv[1], "stop") == 0) {
		ret = usbmon_sendMsg(usb_usbmon_stop, NULL, 0);
		if (ret == EXIT_SUCCESS) {
			printf("usbmon: capture stopped\n");
		}
		return ret;
	}
	else {
		psh_usbmonHelp(argv[0]);
		return EXIT_FAILURE;
	}
}


void __attribute__((constructor)) usbmon_registerapp(void)
{
	static psh_appentry_t app = { .name = "usbmon", .run = psh_usbmon, .info = psh_usbmonInfo };
	psh_registerapp(&app);
}
