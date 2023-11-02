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

#include <imx6ull-flashsrv.h>

#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/msg.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/reboot.h>
#include <sys/platform.h>

#include "../common/sdp.h"

#include "bcb.h"
#include "flashmng.h"


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

/* FUSE BITS */
#define FUSE_WATCHDOG 0x1


enum { ocotp_ctrl, ocotp_ctrl_set, ocotp_ctrl_clr, ocotp_ctrl_tog, ocotp_timing, ocotp_data = 0x8, ocotp_read_ctrl = 0xc,
	ocotp_read_fuse_data = 0x10, ocotp_sw_sticky = 0x14, ocotp_scs = 0x18, ocotp_scs_set, ocotp_scs_clr, ocotp_scs_tog,
	ocotp_crc_addr = 0x1c, ocotp_crc_value = 0x20, ocotp_version = 0x24, ocotp_timing2 = 0x40, ocotp_lock = 0x100,
	ocotp_cfg0 = 0x104, ocotp_cfg1 = 0x108, ocotp_cfg2 = 0x10c, ocopt_cfg3 = 0x110, ocotp_cfg4 = 0x114, ocotp_cfg5 = 0x118,
	ocotp_cfg6 = 0x11c, ocotp_mem0 = 0x120, ocotp_mem1 = 0x124, ocotp_mem2 = 0x128, ocotp_mem3 = 0x12c, ocotp_mem4 = 0x130,
	ocotp_ana0 = 0x134, ocotp_ana1 = 0x138, ocotp_ana2 = 0x13c //TODO: rest of otp shadow regs
	};


struct filedes {
	int fd;
	oid_t oid;
	const char *name;
};


struct {
	int run;

	dbbt_t* dbbt;
	fcb_t *fcb;
	flashsrv_info_t flash;

	char rcvBuff[HID_REPORT_2_SIZE];
	char buff[_PAGE_SIZE * 2]; /* assuming always big enough to fit psd_common.flash.writesz */

	offs_t partsz;
	offs_t partOffs;

	unsigned int nfiles;
	struct filedes *f;
	struct filedes files[FILES_SIZE];
} psd_common;


const usb_hid_dev_setup_t hid_setup = {
	 .dDevice = {
		.bLength = sizeof(usb_device_desc_t), .bDescriptorType = USB_DESC_DEVICE, .bcdUSB = 0x200,
		.bDeviceClass = 0, .bDeviceSubClass = 0, .bDeviceProtocol = 0, .bMaxPacketSize0 = 64,
		.idVendor = 0x15a2, .idProduct = 0x007d, .bcdDevice = 0x0001,
		.iManufacturer = 0, .iProduct = 0, .iSerialNumber = 0,
		.bNumConfigurations = 1
	},
	.dStrMan = {
		.bLength = 27 * 2 + 2,
		.bDescriptorType = USB_DESC_STRING,
		.wData = { 'F', 0, 'r', 0, 'e', 0, 'e', 0, 's', 0, 'c', 0, 'a', 0, 'l', 0, 'e', 0, ' ', 0, 'S', 0, 'e', 0, 'm', 0, 'i', 0, 'C', 0, 'o', 0, 'n', 0, 'd', 0, 'u', 0, 'c', 0, 't', 0, 'o', 0, 'r', 0, ' ', 0, 'I', 0, 'n', 0, 'c', 0 }
	},
	.dStrProd = {
		.bLength = 13 * 2 + 2,
		.bDescriptorType = USB_DESC_STRING,
		.wData = { 'S', 0, 'E', 0, ' ', 0, 'B', 0, 'l', 0, 'a', 0, 'n', 0, 'k', 0, ' ', 0, '6', 0, 'U', 0, 'L', 0, 'L', 0 }
	}
};


static int psd_hidResponse(int err, int type)
{
	int res;

	if (!err) {
		/* Report 3 device to host */
		SET_OPEN_HAB(psd_common.buff);
		if ((res = sdp_send(psd_common.buff[0], psd_common.buff, HID_REPORT_3_SIZE)) < 0)
			err = -eReport3;

		/* Report 4 device to host */
		switch (type) {
		case SDP_WRITE_FILE :
			SET_FILE_COMPLETE(psd_common.buff);
			memset(psd_common.buff + 5, 0, HID_REPORT_4_SIZE - 5);
			if ((res = sdp_send(psd_common.buff[0], psd_common.buff, HID_REPORT_4_SIZE)) < 0)
				err = -eReport4;
			break;

		case SDP_WRITE_REGISTER :
			SET_COMPLETE(psd_common.buff);
			memset(psd_common.buff + 5, 0, HID_REPORT_4_SIZE - 5);
			if ((res = sdp_send(psd_common.buff[0], psd_common.buff, HID_REPORT_4_SIZE)) < 0)
				err = -eReport4;
			break;

		default :
			err = -eReport4;
		}
	}
	else {
		/* Report 3 device to host */
		SET_CLOSED_HAB(psd_common.buff);
		if ((res = sdp_send(psd_common.buff[0], psd_common.buff, HID_REPORT_3_SIZE)) < 0)
			err = -eReport3;

		/* Report 4 device to host */
		SET_HAB_ERROR(psd_common.buff, err);
		memset(psd_common.buff + 5, 0, HID_REPORT_4_SIZE - 5);
		if ((res = sdp_send(psd_common.buff[0], psd_common.buff, HID_REPORT_4_SIZE)) < 0)
			err = -eReport4;
	}

	return err;
}


static int psd_changePartition(uint8_t number)
{
	if (number > psd_common.nfiles) {
		return -eReport1;
	}
	else {
		/* Change file */
		psd_common.f = &psd_common.files[number];

		if (flashmng_getAttr(atSize, &psd_common.partsz, psd_common.f->oid) < 0)
			return -eReport1;

		if (flashmng_getAttr(atDev, &psd_common.partOffs, psd_common.f->oid) < 0)
			return -eReport1;
	}

	printf("PSD: Changing current partition to %s (offs=%lld size=%lld).\n", psd_common.f->name, psd_common.partOffs, psd_common.partsz);

	return hidOK;
}


static int psd_controlBlock(uint32_t block)
{
	int err = hidOK;
	if (block == FCB) {
		printf("PSD: Flash fcb.\n");
		err = fcb_flash(psd_common.f->oid, &psd_common.flash);
	}
	else if (block == DBBT) {
		/* this should be done on whole chip or just kernel partitions */
		printf("PSD: Check bad blocks - start: %u end: %llu.\n", 0, psd_common.flash.size / psd_common.flash.erasesz);
		if ((err = flashmng_checkRange(psd_common.f->oid, 0, psd_common.flash.size, &psd_common.dbbt)) < 0)
			return err;

		/* Change to first partition for flashing */
		psd_changePartition(1);
		printf("PSD: Flash dbbt.\n");
		if (psd_common.dbbt == NULL)
			return -eControlBlock;

		err = dbbt_flash(psd_common.f->oid, psd_common.f->fd, psd_common.dbbt, &psd_common.flash);
	}
	else {
		return -eReport1;
	}

	return err;
}


static int psd_erasePartition(uint32_t size, uint8_t format)
{
	int err = hidOK;

	if (format != 16) {
		printf("PSD: Erase partition - start: %d size: %d (actual size: %llu).\n", 0, size, psd_common.partsz);
		if ((err = flashmng_eraseBlocks(psd_common.f->oid, 0, size)) < 0)
			return err;
	}

	if (format == 8 || format == 16) {
		printf("PSD: Writing JFFS2 CleanMarkers.\n");
		if ((err = flashmng_cleanMarkers(psd_common.f->oid, 0, psd_common.partsz)) < 0)
			return err;
	}

	return err;
}


static int psd_blowFuses(uint32_t fuse)
{
	int err = hidOK;

	uint32_t *base = mmap(NULL, _PAGE_SIZE, PROT_WRITE | PROT_READ, MAP_DEVICE | MAP_PHYSMEM | MAP_ANONYMOUS, -1, OTP_BASE_ADDR);

	printf("PSD: Blowing fuses.\n");

	if (base == NULL) {
		printf("OTP mmap failed\n");
		munmap(base, 0x1000);
		return -1;
	}

	if (*(base + ocotp_ctrl) & OTP_ERROR) {
		printf("OTP error\n");
		munmap(base, _PAGE_SIZE);
		return -1;
	}
	while (*(base + ocotp_ctrl) & OTP_BUSY) usleep(10000);

	/* [4] BT_FUSE_SEL = 1 -> boot from fuses */
	uint32_t val = 0x6;

	/* [21] WDOG_ENABLE = 1 -> enable watchdog in Serial Downloader boot mode
	 * watchdog uses the same addr as the BT_FUSE_SEL */
	if (fuse & FUSE_WATCHDOG)
		val |= (1 << 21);

	*(base + ocotp_ctrl_set) = val | OTP_WR_UNLOCK;
	*(base + ocotp_data) = 0x10;

	if (*(base + ocotp_ctrl) & OTP_ERROR) {
		printf("BT_FUSE_SEL error\n");
		munmap(base, _PAGE_SIZE);
		return -1;
	}
	while (*(base + ocotp_ctrl) & OTP_BUSY) usleep(10000);
	/* Due to internal electrical characteristics of the OTP during writes,
	 * all OTP operations following a write must be separated by 2 us
	 * after the clearing of HW_OCOTP_CTRL_BUSY following the write. */
	usleep(100000);

	*(base + ocotp_ctrl_clr) = val | OTP_WR_UNLOCK;
	while (*(base + ocotp_ctrl) & OTP_BUSY) usleep(10000);
	usleep(100000);

	/* [BOOT_CFG4][BOOT_CFG3][BOOT_CFG2][BOOT_CFG1] -> use raw NAND for internal boot, 64 pages per block, boot search count = 4 (4 FCB blocks) */
	*(base + ocotp_ctrl_set) = 0x5 | OTP_WR_UNLOCK;
	*(base + ocotp_data) = 0x1090;

	if (*(base + ocotp_ctrl) & OTP_ERROR) {
		printf("BOOT_CFG error\n");
		munmap(base, _PAGE_SIZE);
		return -1;
	}
	while (*(base + ocotp_ctrl) & OTP_BUSY) usleep(10000);
	usleep(100000);

	*(base + ocotp_ctrl_set) = OTP_RELOAD;

	if (*(base + ocotp_ctrl) & OTP_ERROR) {
		printf("RELOAD error\n");
		munmap(base, _PAGE_SIZE);
		return -1;
	}
	while (*(base + ocotp_ctrl) & OTP_BUSY) usleep(10000);
	usleep(100000);

	printf("PSD: Fuses blown.\n");
	munmap(base, _PAGE_SIZE);

	return err;
}


static int psd_writeRegister(sdp_cmd_t *cmd)
{
	int err = hidOK;
	int address = (int)cmd->address;
	uint32_t data = cmd->data;
	uint8_t format = cmd->format;

	if (address == CHANGE_PARTITION) {
		err = psd_changePartition(data);
	}
	else if (address == CONTROL_BLOCK_ADDRESS) {
		err = psd_controlBlock(data);
	}
	else if (address == ERASE_PARTITION_ADDRESS) {
		err = psd_erasePartition(data, format);
	}
#if 0 /* obsolete - do not reuse the addresses */
	else if (address == ERASE_CHIP_ADDRESS) {
		err = psd_eraseChip(data);
	}
	else if (address == CHECK_PRODUCTION) {
		err = psd_checkProduction();
	}
#endif
	else if (address == BLOW_FUSES) {
		err = psd_blowFuses(data);
	}
	else if (address == CLOSE_PSD) {
		psd_common.run = 0;
	}
	else {
		printf("PSD: Unrecognized register address: %d.\n", address);
		err = -eReport1;
	}

	err = psd_hidResponse(err, SDP_WRITE_REGISTER);

	return err;
}


static int psd_writeFile(sdp_cmd_t *cmd)
{
	int res, err = hidOK, buffOffset = 0, badBlock = 0;
	offs_t writesz, fileOffs = cmd->address;
	char *outdata = NULL;

	/* Check command parameters */
	if (fileOffs % psd_common.flash.writesz != 0) {
		return -eReport1;
	}

	if (fileOffs + cmd->datasz > psd_common.partsz) {
		return -eReport1;
	}

	if (lseek(psd_common.f->fd, fileOffs, SEEK_SET) < 0) {
		return -eReport1;
	}

	printf("PSD: Writing file.\n");

	/* Receive and write file */
	for (writesz = 0; !err && (writesz < cmd->datasz); writesz += buffOffset) {

		memset(psd_common.buff, 0xff, psd_common.flash.writesz);
		buffOffset = 0;

		while ((buffOffset < psd_common.flash.writesz) && (writesz + buffOffset < cmd->datasz)) {
			if ((res = sdp_recv(1, psd_common.rcvBuff, HID_REPORT_2_SIZE, &outdata)) < 0 ) {
				err = -eReport2;
				break;
			}
			memcpy(psd_common.buff + buffOffset, outdata, res);
			buffOffset += res;
		}

		if (buffOffset && !err) {

			/* check for badblocks - TODO: test it */
			if (fileOffs % psd_common.flash.erasesz == 0) {
				while (flashmng_isBadBlock(psd_common.f->oid, fileOffs)) {
					printf("writeFile: badblock at offs: 0x%x\n", (uint32_t)fileOffs);

					/* Direct write - abort without error */
					if (cmd->format == 1) {
						badBlock = 1;
						break;
					}

					/* badblock - skip it */
					if (lseek(psd_common.f->fd, psd_common.flash.erasesz, SEEK_CUR) < 0) {
						err = -eReport2;
						break;
					}

					fileOffs += psd_common.flash.erasesz;
				}
			}

			/* Direct write - abort due to bad block */
			if (badBlock == 1) {
				break;
			}

			if (!err && ((res = write(psd_common.f->fd, psd_common.buff, psd_common.flash.writesz)) != psd_common.flash.writesz)) {
				err = -eReport2;
				break;
			}

			fileOffs += buffOffset;
		}
		else {
			err = -eReport2;
		}
	}

	return psd_hidResponse(err, SDP_WRITE_FILE);
}


static void *wdg_kicker_thr(void *args)
{
	unsigned timeout = (unsigned)args;

	while (1) {
		wdgreload();
		usleep(timeout * 1000L);
	}

	return args;
}


static void usage(char *progname)
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
		while (lookup(argv[i], NULL, &psd_common.files[i - 1].oid) < 0)
			usleep(200);
	}

	/* Start watchdog kicker */
	pthread_attr_t wdg_attr;
	pthread_t wdg_thr;
	pthread_attr_init(&wdg_attr);
	unsigned wdg_timeout = 32000;
	if ((err = pthread_create(&wdg_thr, &wdg_attr, wdg_kicker_thr, (void *)wdg_timeout)) < 0)
		return -1;

	printf("PSD: Started psd.\n");

	/* Open files */
	for (psd_common.nfiles = 1; psd_common.nfiles < argc; psd_common.nfiles++) {
		struct filedes *currf = &psd_common.files[psd_common.nfiles - 1];

		currf->name = argv[psd_common.nfiles];
		printf("PSD: Opening partition: %s\n", currf->name);

		if ((currf->fd = open(argv[psd_common.nfiles], O_RDWR)) < 0) {
			fprintf(stderr, "PSD: Can't open file '%s'! errno: (%d)", currf->name, errno);
			return -1;
		}
	}

	psd_common.run = 1;
	psd_changePartition(0);

	flashmng_getInfo(psd_common.f->oid, &psd_common.flash);

	printf("PSD: Initializing USB transport\n");
	if (sdp_init(&hid_setup)) {
		printf("PSD: Couldn't initialize USB transport\n");
		return -1;
	}

	while (psd_common.run) {
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

	for (i = 0; i < psd_common.nfiles; ++i) {
		close(psd_common.files[i].fd);
	}

	free(psd_common.dbbt);

	/* TODO: clean persistent bit to boot from NAND ? */
	reboot(PHOENIX_REBOOT_MAGIC);

	return EOK;
}
