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

#include "hid.h"
#include "sdp.h"


struct {
	int (*rf)(int, char *, unsigned int, char **);
} psd;


int psd_readRegister(void)
{
	return EOK;
}


int psd_writeRegister(void)
{
	return EOK;
}


int psd_writeFile(void)
{
	return EOK;
}


int main(int argc, char **argv)
{
	char data[1024];
	sdp_cmd_t *cmd;

	printf("Initializing USB transport\n");

	hid_init(&psd.rf);

	while (1) {
		psd.rf(0, (void *)data, sizeof(cmd) + 1, (void *)&cmd);

		switch (cmd->type) {
			case SDP_READ_REGISTER:
				psd_readRegister();
				break;
			case SDP_WRITE_REGISTER:
				psd_writeRegister();
			case SDP_WRITE_FILE:
				psd_writeFile();
		}
	}

	return EOK;
}
