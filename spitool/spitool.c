/*
 * Phoenix-RTOS
 *
 * SPI shell tool
 *
 * Copyright 2022 Phoenix Systems
 * Author: Aleksander Kaminski
 *
 * %LICENSE%
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#include <spi-msg.h>


int main(int argc, char *argv[])
{
	int opt, usage = 0, mode = 0, speed = 1000 * 1000, err = 0;
	spimsg_ctx_t ctx;
	int dev = 0, ss = 0;
	unsigned char data[256], dout[256] = { 0 };
	size_t datalen = 0, optpos, i;

	while ((opt = getopt(argc, argv, "d:c:m:s:h")) >= 0) {
		switch (opt) {
			case 'd':
				dev = (int)strtol(optarg, NULL, 10);
				break;

			case 'c':
				ss = (int)strtol(optarg, NULL, 10);
				break;

			case 'm':
				mode = (int)strtol(optarg, NULL, 10);
				break;

			case 's':
				speed = (int)strtol(optarg, NULL, 10);
				break;

			case 'h':
				usage = 1;
				break;

			default:
				usage = 1;
				err = EXIT_FAILURE;
				break;
		}
	}

	if (dev < 0 || ss < 0 || mode < 0 || mode > 3 || speed <= 0) {
		usage = 1;
		err = EXIT_FAILURE;
	}

	if (usage != 0) {
		printf("Usage: %s [-d device number] [-c slave select] [-s speed] [-m mode (0-3)] data\n",
			argv[0]);

		return err;
	}

	ctx.mode = mode;
	ctx.speed = speed;

	if (spimsg_open(dev, ss, &ctx) < 0) {
		fprintf(stderr, "%s: SPI open fail\n", argv[0]);
		return EXIT_FAILURE;
	}

	optpos = optind;
	while (argv[optpos] != NULL && datalen < sizeof(data)) {
		data[datalen] = (unsigned char)strtoul(argv[optpos], NULL, 16);
		++datalen;
		++optpos;
	}

	if (datalen != 0) {
		printf("%s: Data to send: ", argv[0]);
		for (i = 0; i < datalen; ++i) {
			printf("%02x ", data[i]);
		}
		puts("\n");

		if (spimsg_xfer(&ctx, data, datalen, dout, datalen, 0) < 0) {
			fprintf(stderr, "%s: SPI XFER failed\n", argv[0]);
			return EXIT_FAILURE;
		}

		printf("%s: Received: ", argv[0]);
		for (i = 0; i < datalen; ++i) {
			printf("%02x ", dout[i]);
		}
		puts("\n");
	}

	return 0;
}
