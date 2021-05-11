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


#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/msg.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/platform.h>
#include <sys/reboot.h>

#include <imxrt-flashsrv.h>
#include <phoenix/arch/imxrt.h>

#include "../common/sdp.h"

#define HID_REPORT_1_SIZE (sizeof(sdp_cmd_t) + 1)
#define HID_REPORT_2_SIZE 1025
#define HID_REPORT_3_SIZE 5
#define HID_REPORT_4_SIZE 65

#define FLASH_CNT 2

#define INTERNAL_FLASH_NAME "/dev/flash1"
#define EXTERNAL_FLASH_NAME "/dev/flash0"

#define LOG(str, ...) do { if (1) fprintf(stderr, "psd: " str "\n", ##__VA_ARGS__); } while (0)
#define LOG_ERROR(str, ...) do { fprintf(stderr, __FILE__  ":%d error: " str "\n", __LINE__, ##__VA_ARGS__); } while (0)


typedef struct _flash_properties_ {
	oid_t oid;

	uint32_t flashSize;
	uint32_t pageSize;
	uint32_t sectorSize;

} flash_properties_t;


struct {
	flash_properties_t flashMems[FLASH_CNT];

	int run;
	uint8_t flashID;
	char buff[HID_REPORT_2_SIZE];
} psd_common;


const usb_hid_dev_setup_t hid_setup = {
	 .dDevice = {
		.bLength = sizeof(usb_device_desc_t), .bDescriptorType = USB_DESC_DEVICE, .bcdUSB = 0x200,
		.bDeviceClass = 0, .bDeviceSubClass = 0, .bDeviceProtocol = 0, .bMaxPacketSize0 = 64,
		.idVendor = 0x1FC9, .idProduct = 0x0135, .bcdDevice = 0x0001,
		.iManufacturer = 0, .iProduct = 0, .iSerialNumber = 0,
		.bNumConfigurations = 1
	},
	.dStrMan = {
		.bLength = 27 * 2 + 2,
		.bDescriptorType = USB_DESC_STRING,
		.wData = { 'F', 0, 'r', 0, 'e', 0, 'e', 0, 's', 0, 'c', 0, 'a', 0, 'l', 0, 'e', 0, ' ', 0, 'S', 0, 'e', 0, 'm', 0, 'i', 0, 'C', 0, 'o', 0, 'n', 0, 'd', 0, 'u', 0, 'c', 0, 't', 0, 'o', 0, 'r', 0, ' ', 0, 'I', 0, 'n', 0, 'c', 0 }
	},
	.dStrProd = {
		.bLength = 11 * 2 + 2,
		.bDescriptorType = USB_DESC_STRING,
		.wData = { 'S', 0, 'E', 0, ' ', 0, 'B', 0, 'l', 0, 'a', 0, 'n', 0, 'k', 0, ' ', 0, 'R', 0, 'T', 0 }
	}
};


static int psd_syncFlash(oid_t oid)
{
	msg_t msg;
	flash_i_devctl_t *idevctl = NULL;
	flash_o_devctl_t *odevctl = NULL;

	msg.type = mtDevCtl;
	msg.i.data = NULL;
	msg.i.size = 0;
	msg.o.data = NULL;
	msg.o.size = 0;

	idevctl = (flash_i_devctl_t *)msg.i.raw;
	idevctl->type = flashsrv_devctl_sync;
	idevctl->oid = oid;

	odevctl = (flash_o_devctl_t *)msg.o.raw;

	if (msgSend(oid.port, &msg) < 0)
		return -1;

	if (odevctl->err < 0)
		return -1;

	return 0;
}


static int psd_write2Flash(oid_t oid, uint32_t paddr, void *data, int size)
{
	msg_t msg;

	msg.type = mtWrite;
	msg.i.io.oid = oid;
	msg.i.io.offs = paddr;
	msg.i.data = data;
	msg.i.size = size;
	msg.o.data = NULL;
	msg.o.size = 0;

	if (msgSend(oid.port, &msg) < 0)
		return -1;

	if (msg.o.io.err < size)
		return -1;

	return size;
}


static int psd_getFlashProperties(uint8_t flashID)
{
	int res;
	msg_t msg;
	flash_i_devctl_t *idevctl = NULL;
	flash_o_devctl_t *odevctl = NULL;

	msg.type = mtDevCtl;
	msg.i.data = NULL;
	msg.i.size = 0;
	msg.o.data = NULL;
	msg.o.size = 0;

	idevctl = (flash_i_devctl_t *)msg.i.raw;
	idevctl->type = flashsrv_devctl_properties;
	idevctl->oid = psd_common.flashMems[flashID].oid;

	odevctl = (flash_o_devctl_t *)msg.o.raw;

	if ((res = msgSend(psd_common.flashMems[flashID].oid.port, &msg)) < 0)
		return -1;

	if (odevctl->err < 0)
		return -1;

	psd_common.flashMems[flashID].flashSize = odevctl->properties.size;
	psd_common.flashMems[flashID].pageSize = odevctl->properties.psize;
	psd_common.flashMems[flashID].sectorSize = odevctl->properties.ssize;

	return 0;
}


int psd_hidResponse(int err, int type)
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


int psd_changeFlash(uint8_t flashID)
{
	int err = hidOK;
	flash_properties_t *flash = (flash_properties_t *)&psd_common.flashMems[psd_common.flashID];

	if (flashID >= FLASH_CNT) {
		err = -eReport1;
	}
	else {
		if (psd_syncFlash(flash->oid) != 0) {
			err = -eReport2;
			return err;
		}

		LOG("changed current flash to flash_%d", flashID);
		psd_common.flashID = flashID;
	}

	return err;
}


int psd_writeRegister(sdp_cmd_t *cmd)
{
	int err = hidOK;
	int address = (int)cmd->address;
	uint32_t data = cmd->data;

	if (address == CLOSE_PSD) {
		psd_common.run = 0;
	}
	else if (address == CHANGE_FLASH) {
		err = psd_changeFlash(data);
	}
	else {
		LOG_ERROR("Unrecognized register address: %d.\n", address);
		err = -eReport1;
	}

	err = psd_hidResponse(err, SDP_WRITE_REGISTER);

	return err;
}


int psd_writeFile(sdp_cmd_t *cmd)
{
	int res, err = hidOK, offset = 0;
	offs_t writesz;
	char *outdata = NULL;

	flash_properties_t *flash = (flash_properties_t *)&psd_common.flashMems[psd_common.flashID];

	offset = cmd->address;

	/* Receive and write file */
	for (writesz = 0; !err && (writesz < cmd->datasz);) {

		memset(psd_common.buff, 0xff, HID_REPORT_2_SIZE - 1);
		if ((res = sdp_recv(1, psd_common.buff, HID_REPORT_2_SIZE, &outdata)) < 0 ) {
			err = -eReport2;
			break;
		}

		if (res % flash->pageSize )
			res = (res / flash->pageSize + 1) * flash->pageSize;

		if (psd_write2Flash(flash->oid, offset, outdata, res) < res) {
			err = -eReport2;
			break;
		}

		writesz += res;
		offset += res;
	}

	if (psd_syncFlash(flash->oid) != 0)
		err = -eReport2;

	err = psd_hidResponse(err, SDP_WRITE_FILE);

	return err;
}


static void psd_enabelCache(unsigned char enable)
{
	platformctl_t pctl;

	pctl.action = pctl_set;
	pctl.type = pctl_devcache;
	pctl.devcache.state = !!enable;

	platformctl(&pctl);
}


int main(int argc, char **argv)
{
	int i, err;
	sdp_cmd_t *pcmd = NULL;
	char cmdBuff[HID_REPORT_1_SIZE];

	const char *const flashesNames[] = { EXTERNAL_FLASH_NAME, INTERNAL_FLASH_NAME };

	psd_common.run = 1;
	/* Set internal flash */
	psd_common.flashID = 1;

	psd_enabelCache(0);

	if (sdp_init(&hid_setup)) {
		LOG_ERROR("couldn't initialize USB transport.");
		return -1;
	}

	for (i = 0; i < FLASH_CNT; ++i) {
		while (lookup(flashesNames[i], NULL, &psd_common.flashMems[i].oid) < 0)
			usleep(1000);

		if (psd_getFlashProperties(i) < 0 ) {
			LOG_ERROR("couldn't get flash properties.");
			return -1;
		}
	}


	LOG("initialized.");

	while (psd_common.run)
	{
		sdp_recv(0, (char *)cmdBuff, sizeof(*pcmd) + 1, (char **)&pcmd);

		switch (pcmd->type) {
			case SDP_WRITE_REGISTER:
				if ((err = psd_writeRegister(pcmd)) != hidOK) {
					LOG_ERROR("error sdp write register, err: %d.", err);
					return err;
				}
				break;

			case SDP_WRITE_FILE:
				if ((err = psd_writeFile(pcmd)) != hidOK) {
					LOG_ERROR("error sdp write file, err: %d.", err);
					return err;
				}
				break;

			default:
				LOG_ERROR("unrecognized command (%#x)", pcmd->type);
				break;
		}
		usleep(200);
	}

	psd_enabelCache(1);
	sdp_destroy();

	LOG("closing PSD. Device is rebooting.");
	reboot(PHOENIX_REBOOT_MAGIC);

	return EOK;
}
