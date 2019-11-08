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

#include <flashsrv.h>
#include <phoenix/arch/imxrt.h>

#include "../common/hid.h"
#include "../common/sdp.h"


#define SET_OPEN_HAB(b) (b)[0]=3;(b)[1]=0x56;(b)[2]=0x78;(b)[3]=0x78;(b)[4]=0x56;
#define SET_CLOSED_HAB(b) (b)[0]=3;(b)[1]=0x12;(b)[2]=0x34;(b)[3]=0x34;(b)[4]=0x12;
#define SET_COMPLETE(b) (b)[0]=4;(b)[1]=0x12;(b)[2]=0x8a;(b)[3]=0x8a;(b)[4]=0x12;
#define SET_FILE_COMPLETE(b) (b)[0]=4;(b)[1]=0x88;(b)[2]=0x88;(b)[3]=0x88;(b)[4]=0x88;
#define SET_HAB_ERROR(b, err) (b)[0]=4;(b)[1]=err;(b)[2]=0xaa;b[3]=0xaa;(b)[4]=0xaa;


#define HID_REPORT_1_SIZE (sizeof(sdp_cmd_t) + 1)
#define HID_REPORT_2_SIZE 1025
#define HID_REPORT_3_SIZE 5
#define HID_REPORT_4_SIZE 65

#define INTERNAL_FLASH_NAME "/dev/flash2"

#define LOG(str, ...) do { if (1) fprintf(stderr, "psd: " str "\n", ##__VA_ARGS__); } while (0)
#define LOG_ERROR(str, ...) do { fprintf(stderr, __FILE__  ":%d error: " str "\n", __LINE__, ##__VA_ARGS__); } while (0)


struct {
	oid_t oid;

	uint32_t flash_size;
	uint32_t page_size;
	uint32_t sector_size;

	int run;
	char buff[HID_REPORT_2_SIZE];

	int (*rf)(int, char *, unsigned int, char **);
	int (*sf)(int, const char *, unsigned int);
} psd;


const hid_dev_setup_t hid_setup = {
	 .ddev = {
		.len = sizeof(usbclient_desc_dev_t), .desc_type = USBCLIENT_DESC_TYPE_DEV, .bcd_usb = 0x200,
		.dev_class = 0, .dev_subclass = 0, .dev_prot = 0, .max_pkt_sz0 = 64,
		.vend_id = 0x1FC9, .prod_id = 0x0135, .bcd_dev = 0x0001,
		.man_str = 0, .prod_str = 0, .sn_str = 0,
		.num_conf = 1
	},
	.dstrman = {
		.len = 27 * 2 + 2,
		.desc_type = USBCLIENT_DESC_TYPE_STR,
		.string = { 'F', 0, 'r', 0, 'e', 0, 'e', 0, 's', 0, 'c', 0, 'a', 0, 'l', 0, 'e', 0, ' ', 0, 'S', 0, 'e', 0, 'm', 0, 'i', 0, 'C', 0, 'o', 0, 'n', 0, 'd', 0, 'u', 0, 'c', 0, 't', 0, 'o', 0, 'r', 0, ' ', 0, 'I', 0, 'n', 0, 'c', 0 }
	},
	.dstrprod = {
		.len = 11 * 2 + 2,
		.desc_type = USBCLIENT_DESC_TYPE_STR,
		.string = { 'S', 0, 'E', 0, ' ', 0, 'B', 0, 'l', 0, 'a', 0, 'n', 0, 'k', 0, ' ', 0, 'R', 0, 'T', 0 }
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
	idevctl->oid = psd.oid;

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


static int psd_getFlashProperties(oid_t oid)
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
	idevctl->oid = oid;

	odevctl = (flash_o_devctl_t *)msg.o.raw;

	if ((res = msgSend(psd.oid.port, &msg)) < 0)
		return -1;

	if (odevctl->err < 0)
		return -1;

	psd.flash_size = odevctl->properties.fsize;
	psd.page_size = odevctl->properties.psize;
	psd.sector_size = odevctl->properties.ssize;

	return 0;
}


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


int psd_writeRegister(sdp_cmd_t *cmd)
{
	int err = hidOK;
	int address = (int)cmd->address;

	if (address == CLOSE_PSD) {
		psd.run = 0;
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

	offset = cmd->address;

	/* Receive and write file */
	for (writesz = 0; !err && (writesz < cmd->datasz);) {

		memset(psd.buff, 0xff, HID_REPORT_2_SIZE - 1);
		if ((res = psd.rf(1, psd.buff, HID_REPORT_2_SIZE, &outdata)) < 0 ) {
			err = -eReport2;
			break;
		}

		if (res % psd.page_size )
			res = (res / psd.page_size + 1) * psd.page_size;

		if (psd_write2Flash(psd.oid, offset, outdata, res) < res) {
			err = -eReport2;
			break;
		}

		writesz += res;
		offset += res;
	}

	if (psd_syncFlash(psd.oid) != 0)
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
	int err;
	sdp_cmd_t *pcmd = NULL;
	char cmdBuff[HID_REPORT_1_SIZE];

	psd.run = 1;
	psd_enabelCache(0);

	if (hid_init(&psd.rf, &psd.sf, &hid_setup)) {
		LOG_ERROR("couldn't initialize USB transport.");
		return -1;
	}

	while (lookup(INTERNAL_FLASH_NAME, NULL, &psd.oid) < 0)
		usleep(100000);

	if (psd_getFlashProperties(psd.oid) < 0 ) {
		LOG_ERROR("couldn't get flash properties.");
		return -1;
	}

	LOG("initialized.");

	while (psd.run)
	{
		psd.rf(0, (void *)cmdBuff, sizeof(*pcmd) + 1, (void **)&pcmd);

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
	hid_destroy();

	LOG("closing PSD. Device is rebooting.");
	reboot(PHOENIX_REBOOT_MAGIC);

	return EOK;
}
