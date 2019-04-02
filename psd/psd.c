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
#include <string.h>

#include "hid.h"
#include "sdp.h"

#define SEND_BUF_SIZE 65
#define RECV_BUF_SIZE 1025
#define MAX_DEVS 16
#define MAX_NAME 128


enum {
	PSD_DEV_FLASH
};


typedef struct _psd_dev_t {
	int type;
	char name[MAX_NAME + 1];
} __attribute__((packed)) psd_dev_t;


struct {
	int (*rf)(int, char *, unsigned int, char **);
	int (*sf)(int, const char *, unsigned int);
	unsigned int currDev;
	unsigned int devsn;
	psd_dev_t devs[MAX_DEVS];
} psd;


int psd_parseArgs(int argc, char **argv)
{
	if (argc < 2) {
		return -1;
	}

	psd.devsn = 0;
	psd.currDev = 0;
	for (int i = 1; i < argc && psd.devsn < MAX_DEVS; i++) {
		if (!strcmp("-f", argv[i])) {
			memcpy(psd.devs[psd.devsn].name, argv[++i], MAX_NAME);
			psd.devs[psd.devsn].name[MAX_NAME] = '\0';
			psd.devs[psd.devsn++].type = PSD_DEV_FLASH;
		} else {
			printf("Couldn't parse args '%s'\n", argv[i]);
			return -1;
		}
	}

	return EOK;
}


int psd_readRegister(sdp_cmd_t *cmd)
{
	int res, n;
	char buff[SEND_BUF_SIZE] = { 3, 0x56, 0x78, 0x78, 0x56 };
	if ((res = psd.sf(3, buff, 5)) < 0) {
		return -1;
	}

	int offset = 0;
	char* address = cmd->address ? (char *)cmd->address : (char *)psd.devs;
	int size = cmd->datasz * (cmd->format / 8); /* in readRegister datasz means register count not bytes */
	buff[0] = 4;
	while (offset < size) {
		n = (SEND_BUF_SIZE - 1 > size - offset) ? (size - offset) : (SEND_BUF_SIZE - 1);
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
	char buff[SEND_BUF_SIZE] = { 3, 0x56, 0x78, 0x78, 0x56 };
	if ((res = psd.sf(3, buff, 5)) < 0) {
		return -1;
	}

	if (cmd->address) {
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
				buff[0] = 4;
				buff[1] = 0x12;
				buff[2] = 0x34;
				buff[3] = 0x34;
				buff[4] = 0x12;
				printf("Failed to write register contents\n");
				return psd.sf(4, buff, SEND_BUF_SIZE);
		}
	} else {
		psd.currDev = cmd->data % psd.devsn;
	}

	buff[0] = 4;
	buff[1] = 0x12;
	buff[2] = 0x8a;
	buff[3] = 0x8a;
	buff[4] = 0x12;
	if((res = psd.sf(4, buff, SEND_BUF_SIZE)) < 0) {
		printf("Failed to send complete status\n");
		return res;
	}

	return EOK;
}


int psd_writeFile(sdp_cmd_t *cmd)
{
	int res, n;
	char buff[RECV_BUF_SIZE] = { 0 };
	int offset = 0;
	char *address = (char *)cmd->address;
	char *outdata;
	int size = cmd->datasz;

	while (offset < size) {
		n = (RECV_BUF_SIZE - 1 > size - offset) ? (size - offset) : (RECV_BUF_SIZE - 1);
		if((res = psd.rf(1, buff, n, &outdata) < 0)) {
			printf("Failed to receive file contents\n");
			return -1;
		}
		memcpy(address + offset, outdata, res);
		offset += n;
	}

	buff[0] = 4;
	buff[1] = 0x88;
	buff[2] = 0x88;
	buff[3] = 0x88;
	buff[4] = 0x88;
	if((res = psd.sf(4, buff, 5)) < 0) {
		printf("Failed to send write file status\n");
		return res;
	}

	return EOK;
}


int main(int argc, char **argv)
{
	if (psd_parseArgs(argc, argv))
		return -1;

	char data[1024];
	sdp_cmd_t *cmd;

	printf("Initializing USB transport\n");
	if (hid_init(&psd.rf, &psd.sf)) {
		printf("Couldn't initialize USB transport\n");
		return -1;
	}

	while (1) {
		psd.rf(0, (void *)data, sizeof(cmd) + 1, (void *)&cmd);

		switch (cmd->type) {
			case SDP_READ_REGISTER:
				psd_readRegister(cmd);
				break;
			case SDP_WRITE_REGISTER:
				psd_writeRegister(cmd);
				break;
			case SDP_WRITE_FILE:
				psd_writeFile(cmd);
				break;
		}
	}

	return EOK;
}
