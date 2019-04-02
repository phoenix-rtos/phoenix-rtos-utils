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
	int size = cmd->datasz * (cmd->format / 8); /* in readRegister datasz means register count not bytes */
	buff[0] = 4;
	while (offset < size) {
		n = (BUF_SIZE - 1 > size - offset) ? (size - offset) : (BUF_SIZE - 1);
		memcpy(buff + 1, address + offset, n);
		offset += n;
		if((res = psd.sf(4, buff, n)) < 0) {
			printf("Failed to send register contents\n");
			return res;
		}
	}

	return EOK;
}


int psd_writeRegister(sdp_cmd_t *cmd)
{
	int res;
	char buff[BUF_SIZE] = { 3, 0x56, 0x78, 0x78, 0x56 };
	if ((res = psd.sf(3, buff, 5)) < 0) {
		return -1;
	}

	switch (cmd->format) {
		case 8:
			*((u8 *)cmd->address) = cmd->data & 0xff;
			break;
		case 16:
			*((u16 *)cmd->address) = cmd->data & 0xffff;
			break;
		case 32:
			*((u32 *)cmd->address) = cmd->data;
			break;
		default:
			buff[1] = 0x12;
			buff[2] = 0x34;
			buff[3] = 0x34;
			buff[4] = 0x12;
			printf("Failed to write register contents\n");
			return psd.sf(4, buff, BUF_SIZE);
	}

	buff[1] = 0x12;
	buff[2] = 0x8a;
	buff[3] = 0x8a;
	buff[4] = 0x12;
	if((res = psd.sf(4, buff, BUF_SIZE)) < 0) {
		printf("Failed to send complete status\n");
		return res;
	}

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
				psd_writeRegister(cmd);
			case SDP_WRITE_FILE:
				psd_writeFile();
		}
	}

	return EOK;
}
