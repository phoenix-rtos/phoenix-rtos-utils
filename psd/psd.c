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

#define SET_OPEN_HAB(b) (b)[0]=3;(b)[1]=0x56;(b)[2]=0x78;(b)[3]=0x78;(b)[4]=0x56;
#define SET_CLOSED_HAB(b) (b)[0]=3;(b)[1]=0x12;(b)[2]=0x34;(b)[3]=0x34;(b)[4]=0x12;
#define SET_COMPLETE(b) (b)[0]=4;(b)[1]=0x12;(b)[2]=0x8a;(b)[3]=0x8a;(b)[4]=0x12;

struct {
	int (*rf)(int, char *, unsigned int, char **);
	int (*sf)(int, const char *, unsigned int);

	unsigned int nfiles;
	FILE *f;
	FILE *files[16];
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
		err = -1;
	}

	if (err || (fseek(psd.f, cmd->address, SEEK_SET) < 0))
		err = -2;

	for (offs = 0, n = 0; !err && (offs < cmd->datasz); offs += n) {

		/* Read data from file */
		n = min(cmd->datasz - offs, res);
		for (l = 0; l < n;) {
			if ((res = fread(buff, n, 1, psd.f)) < 0) {
				err = -2;
				break;
			}
			l += res;
		}
		/* Send data to serial device */
		if ((res = psd.sf(1, buff, sizeof(buff)) < 0)) {
			err = -1;
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
		err = -1;
	}

	if (!err) {
		/* Change file */
		psd.f = psd.files[(int)buff[1]];
		/* Send HAB status */
		SET_OPEN_HAB(buff);
		if ((res = psd.sf(buff[0], buff, 5)) < 0) {
			err = -2;
		} else {
			/* Send complete status */
			SET_CLOSED_HAB(buff);
			if ((res = psd.sf(buff[0], buff, 5)) < 0) {
				err = -3;
			}
		}
	} else {
		SET_COMPLETE(buff);
		if ((res = psd.sf(buff[0], buff, 5)) < 0)
			err = -4;
	}

	return err;
}


int psd_writeFile(sdp_cmd_t *cmd)
{
	int res, err = 0;
	offs_t offs, n, l;
	char buff[1025];
	char *outdata;

	if (fseek(psd.f, cmd->address, SEEK_SET) < 0)
		err = -2;

	for (offs = 0, n = 0; !err && (offs < cmd->datasz); offs += n) {

		/* Read data from serial device */
		if ((res = psd.rf(1, buff, sizeof(buff), &outdata) < 0)) {
			err = -1;
			break;
		}

		/* Write data to file */
		n = min(cmd->datasz - offs, res);
		for (l = 0; l < n;) {
			if ((res = fwrite(outdata + l, n, 1, psd.f)) < 0) {
				err = -2;
				break;
			}
			l += res;
		}
	}

	/* Handle errors */
	if (!err) {
		buff[0] = 4;
		memset(buff + 1, 0x88, 4);
	}
	else {
		buff[0] = 4;
		memset(buff + 1, 0x88, 4);
	}

	/* Send write status */
	if ((res = psd.sf(buff[0], buff, 5)) < 0)
		err = -3;

	return err;
}


int main(int argc, char **argv)
{
	char data[11];
	sdp_cmd_t *pcmd = NULL;

	if (argc < 2) {
		usage(argv[0]);
		return -1;
	}

	/* Open files */
	for (psd.nfiles = 1; psd.nfiles < argc; psd.nfiles++) {
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
