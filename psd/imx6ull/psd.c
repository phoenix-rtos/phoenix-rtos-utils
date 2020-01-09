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

#include <flashsrv.h>

#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/msg.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/reboot.h>

#include "../common/sdp.h"

#include "bcb.h"
#include "flashmng.h"


#define SET_OPEN_HAB(b) (b)[0]=3;(b)[1]=0x56;(b)[2]=0x78;(b)[3]=0x78;(b)[4]=0x56;
#define SET_CLOSED_HAB(b) (b)[0]=3;(b)[1]=0x12;(b)[2]=0x34;(b)[3]=0x34;(b)[4]=0x12;
#define SET_COMPLETE(b) (b)[0]=4;(b)[1]=0x12;(b)[2]=0x8a;(b)[3]=0x8a;(b)[4]=0x12;
#define SET_FILE_COMPLETE(b) (b)[0]=4;(b)[1]=0x88;(b)[2]=0x88;(b)[3]=0x88;(b)[4]=0x88;
#define SET_HAB_ERROR(b, err) (b)[0]=4;(b)[1]=err;(b)[2]=0xaa;b[3]=0xaa;(b)[4]=0xaa;


/* Control blocks */
#define FCB 1
#define DBBT 2

#define FILES_SIZE 16

#define HID_REPORT_1_SIZE (sizeof(sdp_cmd_t) + 1)
#define HID_REPORT_2_SIZE 1025
#define HID_REPORT_3_SIZE 5
#define HID_REPORT_4_SIZE 65

/* OTP PARAMETERS */
#define OTP_BASE_ADDR 0x21BC000
#define IPG_CLK_RATE (66 * 1000 * 1000)

#define OTP_BUSY	0x100
#define OTP_ERROR	0x200
#define OTP_RELOAD	0x400
#define OTP_WR_UNLOCK (0x3e77 << 16)


enum { ocotp_ctrl, ocotp_ctrl_set, ocotp_ctrl_clr, ocotp_ctrl_tog, ocotp_timing, ocotp_data = 0x8, ocotp_read_ctrl = 0xc,
	ocotp_read_fuse_data = 0x10, ocotp_sw_sticky = 0x14, ocotp_scs = 0x18, ocotp_scs_set, ocotp_scs_clr, ocotp_scs_tog,
	ocotp_crc_addr = 0x1c, ocotp_crc_value = 0x20, ocotp_version = 0x24, ocotp_timing2 = 0x40, ocotp_lock = 0x100,
	ocotp_cfg0 = 0x104, ocotp_cfg1 = 0x108, ocotp_cfg2 = 0x10c, ocopt_cfg3 = 0x110, ocotp_cfg4 = 0x114, ocotp_cfg5 = 0x118,
	ocotp_cfg6 = 0x11c, ocotp_mem0 = 0x120, ocotp_mem1 = 0x124, ocotp_mem2 = 0x128, ocotp_mem3 = 0x12c, ocotp_mem4 = 0x130,
	ocotp_ana0 = 0x134, ocotp_ana1 = 0x138, ocotp_ana2 = 0x13c //TODO: rest of otp shadow regs
	};


struct {
	int run;

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


const hid_dev_setup_t hid_setup = {
	.ddev = {
		.len = sizeof(usbclient_desc_dev_t), .desc_type = USBCLIENT_DESC_TYPE_DEV, .bcd_usb = 0x200,
		.dev_class = 0, .dev_subclass = 0, .dev_prot = 0, .max_pkt_sz0 = 64,
		.vend_id = 0x15a2, .prod_id = 0x007d, .bcd_dev = 0x0001,
		.man_str = 0, .prod_str = 0, .sn_str = 0,
		.num_conf = 1
	},
	.dstrman = {
		.len = 27 * 2 + 2,
		.desc_type = USBCLIENT_DESC_TYPE_STR,
		.string = { 'F', 0, 'r', 0, 'e', 0, 'e', 0, 's', 0, 'c', 0, 'a', 0, 'l', 0, 'e', 0, ' ', 0, 'S', 0, 'e', 0, 'm', 0, 'i', 0, 'C', 0, 'o', 0, 'n', 0, 'd', 0, 'u', 0, 'c', 0, 't', 0, 'o', 0, 'r', 0, ' ', 0, 'I', 0, 'n', 0, 'c', 0 }
	},
	.dstrprod = {
		.len = 13 * 2 + 2,
		.desc_type = USBCLIENT_DESC_TYPE_STR,
		.string = { 'S', 0, 'E', 0, ' ', 0, 'B', 0, 'l', 0, 'a', 0, 'n', 0, 'k', 0, ' ', 0, '6', 0, 'U', 0, 'L', 0, 'L', 0 }
	}
};


int psd_hidResponse(int err, int type)
{
	int res;

	if (!err) {
		/* Report 3 device to host */
		SET_OPEN_HAB(psd.buff);
		if ((res = sdp_send(psd.buff[0], psd.buff, HID_REPORT_3_SIZE)) < 0)
			err = -eReport3;

		/* Report 4 device to host */
		switch (type) {
		case SDP_WRITE_FILE :
			SET_FILE_COMPLETE(psd.buff);
			memset(psd.buff + 5, 0, HID_REPORT_4_SIZE - 5);
			if ((res = sdp_send(psd.buff[0], psd.buff, HID_REPORT_4_SIZE)) < 0)
				err = -eReport4;
			break;

		case SDP_WRITE_REGISTER :
			SET_COMPLETE(psd.buff);
			memset(psd.buff + 5, 0, HID_REPORT_4_SIZE - 5);
			if ((res = sdp_send(psd.buff[0], psd.buff, HID_REPORT_4_SIZE)) < 0)
				err = -eReport4;
			break;

		default :
			err = -eReport4;
		}
	}
	else {
		/* Report 3 device to host */
		SET_CLOSED_HAB(psd.buff);
		if ((res = sdp_send(psd.buff[0], psd.buff, HID_REPORT_3_SIZE)) < 0)
			err = -eReport3;

		/* Report 4 device to host */
		SET_HAB_ERROR(psd.buff, err);
		memset(psd.buff + 5, 0, HID_REPORT_4_SIZE - 5);
		if ((res = sdp_send(psd.buff[0], psd.buff, HID_REPORT_4_SIZE)) < 0)
			err = -eReport4;
	}

	return err;
}


int psd_changePartition(uint8_t number)
{
	int err = hidOK;

	if (number > psd.nfiles) {
		err = -eReport1;
	}
	else {
		/* Change file */
		printf("PSD: Changed current partition to flash%d.\n", number);
		psd.f = psd.files[number];
		psd.oid = psd.oids[number];
	}

	return err;
}


int psd_controlBlock(uint32_t block)
{
	int err = hidOK;
	if (block == FCB) {
		printf("PSD: Flash fcb.\n");
		err = fcb_flash(psd.oid, psd.fcb);
	}
	else if (block == DBBT) {
		printf("PSD: Flash dbbt.\n");
		if (psd.dbbt == NULL)
			return -eControlBlock;

		err = dbbt_flash(psd.oid, psd.f, psd.dbbt);
	}
	else {
		return -eReport1;
	}

	return err;
}


int psd_eraseFS(uint32_t size)
{
	int err = hidOK;

	printf("PSD: Erase FS - start: %d end: %d.\n", 0, 64 + (2 * size));
	if ((err = flashmng_eraseBlock(psd.oid, 0, 64 + (2 * size))) < 0)
		return err;

	printf("PSD: Check dbbt - start: %d end: %d.\n", 0, 64 + (2 * size));
	if ((err = flashmng_checkRange(psd.oid, 0, 64 + (2 * size), &psd.dbbt)) < 0)
		return err;

	return err;
}


int psd_eraseAll(uint32_t size)
{
	int err = hidOK;

	printf("PSD: Erase all - start: %d end: %d.\n", 0, BLOCKS_CNT);
	if ((err = flashmng_eraseBlock(psd.oid, 0, BLOCKS_CNT)) < 0)
		return err;

	printf("PSD: Check dbbt - start: %d end: %d.\n", 0, BLOCKS_CNT);
	if ((err = flashmng_checkRange(psd.oid, 0, BLOCKS_CNT, &psd.dbbt)) < 0)
		return err;

	printf("PSD: CleanMakers.\n");
	if ((err = flashmng_cleanMakers(psd.oid, 64 + (2 * size), BLOCKS_CNT)) < 0)
		return err;

	return err;
}


int psd_checkProduction(void)
{
	int err = hidOK;

	printf("PSD: Check dbbt - start: %d end: %d.\n", 0, BLOCKS_CNT);
	if ((err = flashmng_checkRange(psd.oid, 0, BLOCKS_CNT, &psd.dbbt)) < 0)
		return err;

	return err;
}


int psd_blowFuses(void)
{
	int err = hidOK;

	uint32_t *base = mmap(NULL, 0x1000, PROT_WRITE | PROT_READ, MAP_DEVICE, OID_PHYSMEM, OTP_BASE_ADDR);

	if (base == NULL) {
		printf("OTP mmap failed\n");
		munmap(base, 0x1000);
		return -1;
	}

	if (*(base + ocotp_ctrl) & OTP_ERROR) {
		printf("OTP error\n");
		munmap(base, 0x1000);
		return -1;
	}
	while (*(base + ocotp_ctrl) & OTP_BUSY) usleep(10000);

	*(base + ocotp_ctrl_set) = 0x6 | OTP_WR_UNLOCK;
	*(base + ocotp_data) = 0x10;

	if (*(base + ocotp_ctrl) & OTP_ERROR) {
		printf("BT_FUSE_SEL error\n");
		munmap(base, 0x1000);
		return -1;
	}
	while (*(base + ocotp_ctrl) & OTP_BUSY) usleep(10000);
	usleep(100000);

	*(base + ocotp_ctrl_clr) = 0x6 | OTP_WR_UNLOCK;
	while (*(base + ocotp_ctrl) & OTP_BUSY) usleep(10000);
	usleep(100000);

	*(base + ocotp_ctrl_set) = 0x5 | OTP_WR_UNLOCK;
	*(base + ocotp_data) = 0x1090;

	if (*(base + ocotp_ctrl) & OTP_ERROR) {
		printf("BOOT_CFG error\n");
		munmap(base, 0x1000);
		return -1;
	}
	while (*(base + ocotp_ctrl) & OTP_BUSY) usleep(10000);
	usleep(100000);

	*(base + ocotp_ctrl_set) = OTP_RELOAD;

	if (*(base + ocotp_ctrl) & OTP_ERROR) {
		printf("RELOAD error\n");
		munmap(base, 0x1000);
		return -1;
	}
	while (*(base + ocotp_ctrl) & OTP_BUSY) usleep(10000);
	usleep(100000);

	printf("PSD: Fuses blown.\n");
	munmap(base, 0x1000);

	return err;
}


int psd_writeRegister(sdp_cmd_t *cmd)
{
	int err = hidOK;
	int address = (int)cmd->address;
	uint32_t data = cmd->data;

	if (address == CHANGE_PARTITION) {
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
	else if (address == CHECK_PRODUCTION) {
		err = psd_checkProduction();
	}
	else if (address == BLOW_FUSES) {
		err = psd_blowFuses();
	}
	else if (address == CLOSE_PSD) {
		psd.run = 0;
	}
	else {
		printf("PSD: Unrecognized register address: %d.\n", address);
		err = -eReport1;
	}

	err = psd_hidResponse(err, SDP_WRITE_REGISTER);

	return err;
}


int psd_writeFile(sdp_cmd_t *cmd)
{
	int res, err = hidOK, buffOffset = 0;
	offs_t writesz, fileOffs, partsz, partOffs;
	uint32_t pagenum, blocknum;

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

	if (!err && (psd.dbbt == NULL))
		err = -eReport1;

	printf("PSD: Writing file.\n");

	/* Receive and write file */
	for (writesz = 0; !err && (writesz < cmd->datasz); writesz += buffOffset) {

		memset(psd.buff, 0xff, FLASH_PAGE_SIZE);
		res = HID_REPORT_2_SIZE - 1;
		buffOffset = 0;

		while ((buffOffset < FLASH_PAGE_SIZE) && (res == (HID_REPORT_2_SIZE - 1))) {
			if ((res = sdp_recv(1, psd.rcvBuff, HID_REPORT_2_SIZE, &outdata)) < 0 ) {
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
	printf("PSD: Usage: %s <device_1> [device_2] ... [device_n] \n", progname);
}


int main(int argc, char **argv)
{
	int i, err;
	char cmdBuff[HID_REPORT_1_SIZE];
	sdp_cmd_t *pcmd = NULL;

	if (argc < 2 || argc > FILES_SIZE) {
		printf("PSD: Wrong number of inputs arg\n");
		usage(argv[0]);
		return -1;
	}

	printf("PSD: Waiting on flash srv.\n");
	for (i = 1; i < argc; ++i) {
		while (lookup(argv[i], NULL, &psd.oids[i - 1]) < 0)
			usleep(200);
	}

	printf("PSD: Started psd.\n");

	/* Open files */
	for (psd.nfiles = 1; psd.nfiles < argc; psd.nfiles++) {
		printf("PSD: Opened partition: %s\n", argv[psd.nfiles]);
		if ((psd.files[psd.nfiles - 1] = fopen(argv[psd.nfiles], "r+")) == NULL) {
			fprintf(stderr, "PSD: Can't open file '%s'! errno: (%d)", argv[psd.nfiles], errno);
			return -1;
		}
	}

	psd.run = 1;
	psd.fcb = malloc(sizeof(fcb_t));
	psd.f = psd.files[0];
	psd.oid = psd.oids[0];

	printf("PSD: Initializing USB transport\n");
	if (sdp_init(&hid_setup)) {
		printf("PSD: Couldn't initialize USB transport\n");
		return -1;
	}

	while (psd.run) {
		sdp_recv(0, (char *)cmdBuff, sizeof(*pcmd) + 1, (char **)&pcmd);

		switch (pcmd->type) {
			case SDP_WRITE_REGISTER:
				if ((err = psd_writeRegister(pcmd)) != hidOK) {
					printf("PSD: Error during sdp write register, err: %d\n", err);
					return err;
				}
				break;
			case SDP_WRITE_FILE:
				if ((err = psd_writeFile(pcmd)) != hidOK) {
					printf("PSD: error during sdp write file, err: %d\n", err);
					return err;
				}
				break;
			default:
				printf("PSD: Unrecognized command (%#x)\n", pcmd->type);
				break;
		}
	}

	printf("\n------------------\n");
	printf("Close PSD. Device is rebooting.\n");

	for (i = 0; i < psd.nfiles; ++i) {
		fclose(psd.files[i]);
	}

	free(psd.fcb);
	free(psd.dbbt);

	reboot(PHOENIX_REBOOT_MAGIC);

	return EOK;
}
