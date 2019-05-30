/*
 * Phoenix-RTOS
 *
 * psd - Serial Download Protocol client
 *
 * Copyright 2019 Phoenix Systems
 * Author: Bartosz Ciesla, Pawel Pisarczyk, Hubert Buczynski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <usbclient.h>
#include <flashsrv.h>

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/msg.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/mman.h>

#include "hid.h"
#include "sdp.h"
#include "bcb.h"
#include "flashmng.h"

#define SET_OPEN_HAB(b) (b)[0]=3;(b)[1]=0x56;(b)[2]=0x78;(b)[3]=0x78;(b)[4]=0x56;
#define SET_CLOSED_HAB(b) (b)[0]=3;(b)[1]=0x12;(b)[2]=0x34;(b)[3]=0x34;(b)[4]=0x12;
#define SET_COMPLETE(b) (b)[0]=4;(b)[1]=0x12;(b)[2]=0x8a;(b)[3]=0x8a;(b)[4]=0x12;
#define SET_FILE_COMPLETE(b) (b)[0]=4;(b)[1]=0x88;(b)[2]=0x88;(b)[3]=0x88;(b)[4]=0x88;
#define SET_HAB_ERROR(b, err) (b)[0]=4;(b)[1]=err;(b)[2]=0xaa;b[3]=0xaa;(b)[4]=0xaa;

/* Addresses definitions for WRITE_REGISTER */
#define PSD_ADDRESS -1
#define ERASE_ROOTFS_ADDRESS -2
#define ERASE_ALL_ADDRESS -3
#define CONTROL_BLOCK_ADDRESS -4

/* Control blocks */
#define FCB 1
#define DBBT 2

#define FILES_SIZE 16

#define HID_REPORT_1_SIZE sizeof(sdp_cmd_t) + 1
#define HID_REPORT_2_SIZE 1025
#define HID_REPORT_3_SIZE 5
#define HID_REPORT_4_SIZE 65


struct {
	int (*rf)(int, char *, unsigned int, char **);
	int (*sf)(int, const char *, unsigned int);

	dbbt_t* dbbt;
	fcb_t *fcb;

	char rcvBuff[HID_REPORT_2_SIZE];
	char buff[FLASH_PAGE_SIZE];

	unsigned int nfiles;
	FILE *f;
	oid_t oid;

	FILE *files[FILES_SIZE];
	oid_t oids[FILES_SIZE];
} psd;

int psd_hidResponse(int err, int type)
{
	int res;

	if (!err) {
		/* Report 3 device to host */
		SET_OPEN_HAB(psd.buff);
		if ((res = psd.sf(psd.buff[0], psd.buff, HID_REPORT_3_SIZE)) < 0)
			err = -eReport3;

		/* Report 4 device to host */
		switch (type) {
		case SDP_WRITE_FILE :
			SET_FILE_COMPLETE(psd.buff);
			memset(psd.buff + 5, 0, HID_REPORT_4_SIZE - 5);
			if ((res = psd.sf(psd.buff[0], psd.buff, HID_REPORT_4_SIZE)) < 0)
				err = -eReport4;
			break;

		case SDP_WRITE_REGISTER :
			SET_COMPLETE(psd.buff);
			memset(psd.buff + 5, 0, HID_REPORT_4_SIZE - 5);
			if ((res = psd.sf(psd.buff[0], psd.buff, HID_REPORT_4_SIZE)) < 0)
				err = -eReport4;
			break;

		default :
			err = -eReport4;
		}
	}
	else {
		/* Report 3 device to host */
		SET_CLOSED_HAB(psd.buff);
		if ((res = psd.sf(psd.buff[0], psd.buff, HID_REPORT_3_SIZE)) < 0)
			err = -eReport3;

		/* Report 4 device to host */
		SET_HAB_ERROR(psd.buff, err);
		memset(psd.buff + 5, 0, HID_REPORT_4_SIZE - 5);
		if ((res = psd.sf(psd.buff[0], psd.buff, HID_REPORT_4_SIZE)) < 0)
			err = -eReport4;
	}

	return err;
}
//int psd_readRegister(sdp_cmd_t *cmd)
//{
//	int res, err = 0;
//	offs_t offs, n, l;
//	char buff[65];

//	SET_OPEN_HAB(buff);
//	if ((res = psd.sf(buff[0], buff, 5)) < 0) {
//		err = -eRaport1;
//	}

//	if (err || (fseek(psd.f, cmd->address, SEEK_SET) < 0))
//		err = -eRaport1;

//	for (offs = 0, n = 0; !err && (offs < cmd->datasz); offs += n) {

//		/* Read data from file */
//		n = min(cmd->datasz - offs, res);
//		for (l = 0; l < n;) {
//			if ((res = fread(buff, n, 1, psd.f)) < 0) {
//				err = -eRaport2;
//				break;
//			}
//			l += res;
//		}
//		/* Send data to serial device */
//		if ((res = psd.sf(1, buff, sizeof(buff)) < 0)) {
//			err = -eRaport1;
//			break;
//		}
//	}

//	return err;
//}

int psd_changePartition(u8 number)
{
	int err = hidOK;

	if (number > psd.nfiles) {
		err = -eReport1;
	}
	else {
		/* Change file */
		printf("Changed to flash%d.\n", number);
		psd.f = psd.files[number];
		psd.oid = psd.oids[number];
	}

	return err;
}


int psd_controlBlock(u32 block)
{
	int err = hidOK;
	if (block == FCB) {
		printf("Flash fcb.\n");
		err = fcb_flash(psd.oid, psd.fcb);
	}
	else if (block == DBBT) {
		printf("Flash dbbt.\n");
		err = dbbt_flash(psd.oid, psd.f, psd.dbbt);
	}
	else {
		return -eReport1;
	}

	return err;
}


int psd_eraseFS(u32 size)
{
	int err = hidOK;

	printf("Erase FS - start: %d end: %d.\n", 0, 64 + (2 * size));
	if ((err = flashmng_eraseBlock(psd.oid, 0, 64 + (2 * size))) < 0)
		return err;

	printf("Check range - start: %d end: %d.\n", 0, 64 + (2 * size));
	if ((err = flashmng_checkRange(psd.oid, 0, 64 + (2 * size), &psd.dbbt)) < 0)
		return err;

	return err;
}


int psd_eraseAll(u32 size)
{
	int err = hidOK;

	printf("Erase all - start: %d end: %d.\n", 0, BLOCKS_CNT);
	if ((err = flashmng_eraseBlock(psd.oid, 0, BLOCKS_CNT)) < 0)
		return err;

	printf("Check range - start: %d end: %d.\n", 0, BLOCKS_CNT);
	if ((err = flashmng_checkRange(psd.oid, 0, BLOCKS_CNT, &psd.dbbt)) < 0)
		return err;

	printf("CleanMakers.\n");
	if ((err = flashmng_cleanMakers(psd.oid, 64 + (2 * size), BLOCKS_CNT)) < 0)
		return err;

	printf("After CleanMakers.\n");
	return err;
}


int psd_writeRegister(sdp_cmd_t *cmd)
{
	int err = hidOK;
	int address = (int)cmd->address;
	u32 data = cmd->data;

	if (address == PSD_ADDRESS) {
		err = psd_changePartition(data);
	}
	else if (address == CONTROL_BLOCK_ADDRESS) {
		err = psd_controlBlock(data);
	}
	else if (address == ERASE_ROOTFS_ADDRESS) {
		err = psd_eraseFS(data);
	}
	else if (address == ERASE_ALL_ADDRESS) {
		err = psd_eraseAll(data);
	}
	else {
		err = -eReport1;
	}

	err = psd_hidResponse(err, SDP_WRITE_REGISTER);

	return err;
}


//int psd_dcdWrite(sdp_cmd_t *cmd)
//{
//	int res, err = 0;
//	char buff[1025];
//	char *outdata;

//	/* Read DCD binary data */
//	if ((res = psd.rf(1, buff, sizeof(buff), &outdata) < 0)) {
//		err = -eRaport1;
//	}

//	if (!err) {
//		/* Change file */
//		psd.f = psd.files[(int)buff[1]];
//		//smth missing
//		/* Send HAB status */
//		SET_OPEN_HAB(buff);
//		if ((res = psd.sf(buff[0], buff, 5)) < 0) {
//			err = -eRaport2;
//		} else {
//			/* Send complete status */
//			SET_CLOSED_HAB(buff);
//			if ((res = psd.sf(buff[0], buff, 5)) < 0) {
//				err = -eRaport3;
//			}
//		}
//	} else {
//		SET_COMPLETE(buff);
//		if ((res = psd.sf(buff[0], buff, 5)) < 0)
//			err = -eRaport4;
//	}

//	return err;
//}


int psd_writeFile(sdp_cmd_t *cmd)
{
	int res, err = hidOK, buffOffset = 0;
	offs_t writesz, fileOffs, partsz, partOffs;
	u32 pagenum, blocknum;

	char *outdata = NULL;

	/* Check command parameters */
	fileOffs = cmd->address * FLASH_PAGE_SIZE;
	if (flashmng_getAttr(atSize, &partsz, psd.oid) < 0)
		err = -eReport1;

	if (!err && flashmng_getAttr(atDev, &partOffs, psd.oid) < 0)
		err = -eReport1;

	if (!err && (cmd->datasz + fileOffs) > partsz)
		err = -eReport1;

	if (!err && (fseek(psd.f, fileOffs, SEEK_SET) < 0))
		err = -eReport1;

	printf("Writing file.\n");

	/* Receive and write file */
	for (writesz = 0; !err && (writesz < cmd->datasz); writesz += buffOffset) {

		memset(psd.buff, 0xff, FLASH_PAGE_SIZE);
		res = HID_REPORT_2_SIZE - 1;
		buffOffset = 0;

		while ((buffOffset < FLASH_PAGE_SIZE) && (res == (HID_REPORT_2_SIZE - 1))) {
			if ((res = psd.rf(1, psd.rcvBuff, HID_REPORT_2_SIZE, &outdata)) < 0 ) {
				err = -eReport2;
				break;
			}
			memcpy(psd.buff + buffOffset, outdata, res);
			buffOffset += res;
		}

		if (buffOffset && !err) {

			pagenum = partOffs + fileOffs;
			blocknum = pagenum / PAGES_PER_BLOCK;

			/* If the current block is bad, the block should be omitted. */
			if (!(pagenum % PAGES_PER_BLOCK) && dbbt_block_is_bad(psd.dbbt, blocknum)) {
				if (fseek(psd.f, FLASH_PAGE_SIZE * PAGES_PER_BLOCK, SEEK_SET) < 0)
					err = -eReport2;
			}

			if (!err && ((res = fwrite(psd.buff, sizeof(char), FLASH_PAGE_SIZE, psd.f)) != FLASH_PAGE_SIZE)) {
				err = -eReport2;
				break;
			}

			fileOffs += buffOffset;
		}
		else {
			err = -eReport2;
		}
	}

	err = psd_hidResponse(err, SDP_WRITE_FILE);

	return err;
}


void usage(char *progname)
{
	printf("Usage: %s <device_1> [device_2] ... [device_n] \n", progname);
}


int main(int argc, char **argv)
{
	int i, err;
	char cmdBuff[HID_REPORT_1_SIZE];
	sdp_cmd_t *pcmd = NULL;

	if (argc < 2 || argc > FILES_SIZE) {
		printf("Wrong number of inputs arg\n");
		usage(argv[0]);
		return -1;
	}

	printf("Waiting on flash srv.\n");
	for (i = 1; i < argc; ++i) {
		while (lookup(argv[i], NULL, &psd.oids[i - 1]) < 0)
			usleep(200);
	}
	printf("Started psd.\n");

	/* Open files */
	for (psd.nfiles = 1; psd.nfiles < argc; psd.nfiles++) {
		printf("Opened partition: %s\n", argv[psd.nfiles]);
		if ((psd.files[psd.nfiles - 1] = fopen(argv[psd.nfiles], "r+")) == NULL) {
			fprintf(stderr, "Can't open file '%s'! errno: (%d)", argv[psd.nfiles], errno);
			return -1;
		}
	}

	psd.fcb = malloc(sizeof(fcb_t));
	psd.f = psd.files[0];
	psd.oid = psd.oids[0];

	printf("Initializing USB transport\n");
	if (hid_init(&psd.rf, &psd.sf)) {
		printf("Couldn't initialize USB transport\n");
		return -1;
	}

	while (1) {
		psd.rf(0, (void *)cmdBuff, sizeof(*pcmd) + 1, (void **)&pcmd);

		switch (pcmd->type) {
//			case SDP_READ_REGISTER:
//				if ((err = psd_readRegister(pcmd)) != hidOK) {
//					printf("Error during sdp read register, err: %d", err);
//					return err;
//				}
//				break;
			case SDP_WRITE_REGISTER:
				if ((err = psd_writeRegister(pcmd)) != hidOK) {
					printf("Error during sdp write register, err: %d\n", err);
					return err;
				}
				break;
//			case SDP_DCD_WRITE:
//				if ((err = psd_dcdWrite(pcmd)) != hidOK) {
//					printf("Error during sdp dcd write, err: %d", err);
//					return err;
//				}
//				break;
			case SDP_WRITE_FILE:
				if ((err = psd_writeFile(pcmd)) != hidOK) {
					printf("Error during sdp write file, err: %d\n", err);
					return err;
				}
				break;
			default:
				printf("Unrecognized command (%#x)\n", pcmd->type);
				break;
		}
	}

	for (i=0; i < psd.nfiles; ++i) {
		fclose(psd.files[i]);
	}

	free(psd.fcb);
	free(psd.dbbt);

	return EOK;
}
