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

#define BUF_SIZE 65

struct {
	int (*rf)(int, char *, unsigned int, char **);
	int (*sf)(int, const char *, unsigned int);
} psd;


int psd_readRegister(sdp_cmd_t *cmd)
{
	int res, n;
	char buff[BUF_SIZE] = { 3, 0x56, 0x78, 0x78, 0x56 };
	if ((res = psd.sf(3, buff, 5)) < 0) {
		return -1;
	}

	int offset = 0;
	char* address = (char *)cmd->address;
	buff[0] = 4;
	while (offset < cmd->datasz) {
		n = (BUF_SIZE - 1 > cmd->datasz - offset) ? (cmd->datasz - offset) : (BUF_SIZE - 1);
		memcpy(buff + 1, address + offset, n);
		offset += n;
		if((res = psd.sf(4, buff, n)) < 0) {
			printf("Failed to send image contents\n");
			return res;
		}
	}

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
				psd_readRegister(cmd);
				break;
			case SDP_WRITE_REGISTER:
				psd_writeRegister();
			case SDP_WRITE_FILE:
				psd_writeFile();
		}
	}

	return EOK;
}
