/*
 * Phoenix-RTOS
 *
 * psd - Serial Download Protocol client
 *
 * HID support
 *
 * Copyright 2019 Phoenix Systems
 * Author: Bartosz Ciesla, Pawel Pisarczyk, Hubert Buczynski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */


#include <errno.h>
#include <stdio.h>

#include <arpa/inet.h>

#include "hid.h"
#include "sdp.h"


struct {
	uint8_t len;
	uint8_t type;
	uint8_t data[128];
} __attribute__((packed)) dhidreport = { 2 + 76, USBCLIENT_DESC_TYPE_HID_REPORT,

	/* Raw HID report descriptor - compatibile with IMX6ULL SDP protocol */
	{	0x06, 0x00, 0xff, 0x09, 0x01, 0xa1, 0x01, 0x85, 0x01, 0x19,
		0x01, 0x29, 0x01, 0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08,
		0x95, 0x10, 0x91, 0x02, 0x85, 0x02, 0x19, 0x01, 0x29, 0x01,
		0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x80, 0x95, 0x40, 0x91,
		0x02, 0x85, 0x03, 0x19, 0x01, 0x29, 0x01, 0x15, 0x00, 0x26,
		0xff, 0x00, 0x75, 0x08, 0x95, 0x04, 0x81, 0x02, 0x85, 0x04,
		0x19, 0x01, 0x29, 0x01, 0x15, 0x00, 0x26, 0xff, 0x00, 0x75,
		0x08, 0x95, 0x40, 0x81, 0x02, 0xc0 }
};


usbclient_desc_ep_t dep = { .len = 7, .desc_type = USBCLIENT_DESC_TYPE_ENDPT, .endpt_addr = 0x81, /* direction IN */
	.attr_bmp = 0x03, .max_pkt_sz = 64, .interval = 0x01
};


/* HID descriptor */
struct {
	uint8_t bLength;
	uint8_t bType;
	uint16_t bcdHID;
	uint8_t bCountryCode;
	uint8_t bNumDescriptors;		/* number of descriptors (at least one) */
	uint8_t bDescriptorType;		/* mandatory descriptor type */
	uint16_t wDescriptorLength;
} __attribute__((packed)) dhid = { 9, USBCLIENT_DESC_TYPE_HID, 0x0110, 0x0, 1, 0x22, 76 };


usbclient_desc_intf_t diface = { .len = 9, .desc_type = USBCLIENT_DESC_TYPE_INTF, .intf_num = 0, .alt_set = 0,
	.num_endpt = 1, .intf_class = 0x03, .intf_subclass = 0x00, .intf_prot = 0x00, .intf_str = 2
};


usbclient_desc_conf_t dconfig = { .len = 9, .desc_type = USBCLIENT_DESC_TYPE_CFG,
	.total_len = sizeof(usbclient_desc_conf_t) + sizeof(usbclient_desc_intf_t) + sizeof(dhid) + sizeof(usbclient_desc_ep_t),
	.num_intf = 1, .conf_val = 1, .conf_str = 1, .attr_bmp = 0xc0, .max_pow = 5
};


usbclient_desc_str_zr_t dstr0 = {
	.len = sizeof(usbclient_desc_str_zr_t),
	.desc_type = USBCLIENT_DESC_TYPE_STR,
	.w_langid0 = 0x0409 /* English */
};


usbclient_desc_list_t dev, conf, iface, hid, ep, str0, strman, strprod, hidreport;


static usbclient_conf_t config;


int hid_recv(int what, char *data, unsigned int len, char **outdata)
{
	int res;
	sdp_cmd_t *cmd;

	if ((res = usbclient_receive(&config.endpoint_list.endpoints[0], data, len)) < 0)
		return -1;

	if (!what) {
		if (data[0] != 1)       /* HID report SDP CMD */
			return -1;
		cmd = (sdp_cmd_t *)&data[1];
		cmd->address = ntohl(cmd->address);
		cmd->datasz = ntohl(cmd->datasz);
		cmd->data = ntohl(cmd->data);
	}
	else if (data[0] != 2)		/* HID report SDP CMD DATA */
		return -2;

	*outdata = data + 1;

	return res - 1;
}


int hid_send(int what, const char *data, unsigned int len)
{
	if (what == 3) {	/* HAB security configuration */
		if (data[0] != 3 || len != 5)
			return -1;
	} else if (what == 4) {	/* SDP command response data */
		if (data[0] != 4 || len > 65)
			return -2;
	}

	return usbclient_send(&config.endpoint_list.endpoints[1], data, len);
}


int hid_init(int (**rf)(int, char *, unsigned int, char **), int (**sf)(int, const char *, unsigned int), const hid_dev_setup_t* dev_setup)
{
	int res;

	usbclient_ep_list_t endpoints = {
		.size = 2,
		.endpoints = {
			{ .id = 0, .type = USBCLIENT_ENDPT_TYPE_CONTROL, .direction = 0                     /* for control endpoint it's ignored */},
			{ .id = 1, .type = USBCLIENT_ENDPT_TYPE_INTR, .direction = USBCLIENT_ENDPT_DIR_IN } /* IN interrupt endpoint required for HID */
		}
	};

	dev.size = 1;
	dev.descriptors = (usbclient_desc_gen_t *)&dev_setup->ddev;
	dev.next = &conf;

	conf.size = 1;
	conf.descriptors = (usbclient_desc_gen_t *)&dconfig;
	conf.next = &iface;

	iface.size = 1;
	iface.descriptors = (usbclient_desc_gen_t *)&diface;
	iface.next = &hid;

	hid.size = 1;
	hid.descriptors = (usbclient_desc_gen_t *)&dhid;
	hid.next = &ep;

	ep.size = 1;
	ep.descriptors = (usbclient_desc_gen_t *)&dep;
	ep.next = &str0;

	str0.size = 1;
	str0.descriptors = (usbclient_desc_gen_t *)&dstr0;
	str0.next = &strman;

	strman.size = 1;
	strman.descriptors = (usbclient_desc_gen_t *)&dev_setup->dstrman;
	strman.next = &strprod;

	strprod.size = 1;
	strprod.descriptors = (usbclient_desc_gen_t *)&dev_setup->dstrprod;
	strprod.next = &hidreport;

	hidreport.size = 1;
	hidreport.descriptors = (usbclient_desc_gen_t *)&dhidreport;
	hidreport.next = NULL;

	config.endpoint_list = endpoints;
	config.descriptors_head = &dev;

	if ((res = usbclient_init(&config)) != EOK) {
		return res;
	}

	*rf = hid_recv;
	*sf = hid_send;

	return EOK;
}
