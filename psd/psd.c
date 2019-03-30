/*
 * Phoenix-RTOS
 *
 * psd - Serial Download Protocol client
 *
 * Copyright 2019 Phoenix Systems
 * Author: Bartosz Ciesla, Pawel Pisarczyk
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <errno.h>
#include <stdio.h>
#include <usbclient.h>
#include <fcntl.h>


int psd_writeFile()


int main(int argc, char **argv)
{
	printf("Initializing USB transport\n");
	usbdev_init(&d);

	while (1) {
		d->recv(0, dev);

		switch (cmd.type) {
			case SDP_READ_REG:
				sdp_readReg(dev);
				break;
			case SDP_WRITE_REG:
				sdp_writeReg(dev);
			case SDP_WRITE_FILE:
				sdp_writeFile(dev);
			case SDP_READ_FILE:
				sdp_readFile(dev);
		}
	}

	usbdev_destroy();

	return EOK;
}
