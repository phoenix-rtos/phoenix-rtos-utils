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

struct {
} psd;


int psd_readRegister(void)
{
}


int psd_writeRegister(void)
{
}


int psd_writeFile(void)
{
}


int main(int argc, char **argv)
{
	printf("Initializing USB transport\n");

	hid_init(&psd.rf);

	while (1) {
		d->recv(0, dev);

		switch (cmd.type) {
			case SDP_READ_REGISTER:
				psd_readReg();
				break;
			case SDP_WRITE_REGISTER:
				psd_writeReg();
			case SDP_WRITE_FILE:
				psd_writeFile();
		}
	}

	return EOK;
}
