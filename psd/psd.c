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

#include <usbclient.h>

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/msg.h>
#include <sys/stat.h>
#include <sys/file.h>

#include "hid.h"
#include "sdp.h"

#define SET_OPEN_HAB(b) (b)[0]=3;(b)[1]=0x56;(b)[2]=0x78;(b)[3]=0x78;(b)[4]=0x56;
#define SET_CLOSED_HAB(b) (b)[0]=3;(b)[1]=0x12;(b)[2]=0x34;(b)[3]=0x34;(b)[4]=0x12;
#define SET_COMPLETE(b) (b)[0]=4;(b)[1]=0x12;(b)[2]=0x8a;(b)[3]=0x8a;(b)[4]=0x12;
#define SET_FILE_COMPLETE(b) (b)[0]=4;(b)[1]=0x88;(b)[2]=0x88;(b)[3]=0x88;(b)[4]=0x88;

#define FLASH_PAGE_SIZE 0x1000
#define HID_DATA_SIZE 1025
#define FILES_SIZE 16

struct {
	int (*rf)(int, char *, unsigned int, char **);
	int (*sf)(int, const char *, unsigned int);

	char rcvBuff[HID_DATA_SIZE];
	char buff[FLASH_PAGE_SIZE];

	unsigned int nfiles;
	FILE *f;
	FILE *files[FILES_SIZE];
	oid_t oids[FILES_SIZE];
} psd;


void usage(char *progname)
{
	printf("Usage: %s <device_1> [device_2] ... [device_n] \n", progname);
}


int psd_readRegister(sdp_cmd_t *cmd)
{
	int res, err = 0;
	offs_t offs, n, l;
	char buff[65];

	SET_OPEN_HAB(buff);
	if ((res = psd.sf(buff[0], buff, 5)) < 0) {
		err = -eRaport1;
	}

	if (err || (fseek(psd.f, cmd->address, SEEK_SET) < 0))
		err = -eRaport1;

	for (offs = 0, n = 0; !err && (offs < cmd->datasz); offs += n) {

		/* Read data from file */
		n = min(cmd->datasz - offs, res);
		for (l = 0; l < n;) {
			if ((res = fread(buff, n, 1, psd.f)) < 0) {
				err = -eRaport2;
				break;
			}
			l += res;
		}
		/* Send data to serial device */
		if ((res = psd.sf(1, buff, sizeof(buff)) < 0)) {
			err = -eRaport1;
			break;
		}
	}

	return err;
}


int psd_dcdWrite(sdp_cmd_t *cmd)
{
	int res, err = 0;
	char buff[1025];
	char *outdata;

	/* Read DCD binary data */
	if ((res = psd.rf(1, buff, sizeof(buff), &outdata) < 0)) {
		err = -eRaport1;
	}

	if (!err) {
		/* Change file */
		psd.f = psd.files[(int)buff[1]];
		//smth missing
		/* Send HAB status */
		SET_OPEN_HAB(buff);
		if ((res = psd.sf(buff[0], buff, 5)) < 0) {
			err = -eRaport2;
		} else {
			/* Send complete status */
			SET_CLOSED_HAB(buff);
			if ((res = psd.sf(buff[0], buff, 5)) < 0) {
				err = -eRaport3;
			}
		}
	} else {
		SET_COMPLETE(buff);
		if ((res = psd.sf(buff[0], buff, 5)) < 0)
			err = -eRaport4;
	}

	return err;
}


int psd_getAttr(int type, offs_t* val, FILE* file)
{
	int i;
	msg_t msg;

	for (i = 0; i < FILES_SIZE && file != psd.files[i]; ++i);

	if (i == (FILES_SIZE - 1))
		return -eRaport1;

	msg.type = mtGetAttr;
	msg.i.data = NULL;
	msg.i.size = 0;
	msg.o.data = NULL;
	msg.o.size = 0;

	msg.i.attr.type = type;
	msg.i.attr.oid = psd.oids[i];

	if (msgSend(psd.oids[i].port, &msg) < 0)
		return -eRaport1;

	*val = msg.o.attr.val;

	return hidOK;
}


int psd_writeFile(sdp_cmd_t *cmd)
{
	int res, err = 0, buffOffset = 0;
	offs_t offs, flashoffs, partsz;

	char *outdata = NULL;

	flashoffs = cmd->address * FLASH_PAGE_SIZE;

	if (psd_getAttr(atSize, &partsz, psd.f) < 0)
		err = -eRaport1;

	if (!err && (cmd->datasz + flashoffs) > partsz)
		err = -eRaport1;

	if (!err && (fseek(psd.f, flashoffs, SEEK_SET) < 0))
		err = -eRaport1;

	/* Receive file */
	for (offs = 0; !err && (offs < cmd->datasz); offs += buffOffset) {

		memset(psd.buff, 0xff, FLASH_PAGE_SIZE);
		res = HID_DATA_SIZE - 1;
		buffOffset = 0;

		while ((buffOffset < FLASH_PAGE_SIZE) && (res == (HID_DATA_SIZE - 1))) {

			if ((res = psd.rf(1, psd.rcvBuff, HID_DATA_SIZE, &outdata)) < 0 ) {
				err = -eRaport2;
				break;
			}

			memcpy(psd.buff + buffOffset, outdata, res);
			buffOffset += res;
		}

		if (buffOffset && !err) {
			if ((res = fwrite(psd.buff, sizeof(char), FLASH_PAGE_SIZE, psd.f)) != FLASH_PAGE_SIZE) {
				err = -eRaport2;
				break;
			}
		}
		else {
			err = -eRaport2;
		}
	}


	/* Raport 3 device to host */
	if (!err) {
		SET_OPEN_HAB(psd.buff);
		if ((res = psd.sf(psd.buff[0], psd.buff, 5)) < 0)
			err = -eRaport3;

		printf("Raport 3, res: %d, err: %d\n", res, err);

		/* Raport 4 device to host */
		SET_FILE_COMPLETE(psd.buff);
		if ((res = psd.sf(psd.buff[0], psd.buff, 5)) < 0)
			err = -eRaport4;

		printf("Raport 4, res: %d, err: %d", res, err);
	}
	else {
		//TODO: send raport about error
	}

	return err;
}


int main(int argc, char **argv)
{
	int i;
	char data[11];
	sdp_cmd_t *pcmd = NULL;

	if (argc < 2 || argc > FILES_SIZE) {
		printf("Wrong number of inputs arg\n");
		usage(argv[0]);
		return -1;
	}

	printf("Waiting on flash srv.\n");
	for (i = 1; i < argc; ++i) {
		while (lookup(argv[i], NULL, &psd.oids[i - 1]) < 0)
			sleep(1);
	}

	printf("Started psd\n");

	/* Open files */
	printf("Start psd, reading files\n");
	for (psd.nfiles = 1; psd.nfiles < argc; psd.nfiles++) {
		printf("File name: %s\n", argv[psd.nfiles]);
		if ((psd.files[psd.nfiles - 1] = fopen(argv[psd.nfiles], "r+")) == NULL) {
			fprintf(stderr, "Can't open file '%s'! errno: (%d)", argv[psd.nfiles], errno);
			return -1;
		}
	}
	psd.f = psd.files[0];

	printf("Initializing USB transport\n");
	if (hid_init(&psd.rf, &psd.sf)) {
		printf("Couldn't initialize USB transport\n");
		return -1;
	}


	while (1) {
		psd.rf(0, (void *)data, sizeof(*pcmd) + 1, (void **)&pcmd);

		switch (pcmd->type) {
			case SDP_READ_REGISTER:
				psd_readRegister(pcmd);
				break;
			case SDP_DCD_WRITE:
				psd_dcdWrite(pcmd);
				break;
			case SDP_WRITE_FILE:
				psd_writeFile(pcmd);
				break;
			default:
				printf("Unrecognized command (%d)\n", pcmd->type);
				break;
		}
	}

	return EOK;
}
