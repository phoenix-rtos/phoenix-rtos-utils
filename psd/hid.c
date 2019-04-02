/*
 * Phoenix-RTOS
 *
 * psd - Serial Download Protocol client
 *
 * HID support
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

#include "sdp.h"


struct {
	u8 len;
	u8 type;
	u8 data[128];
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
	u8 bLength;
	u8 bType;
	u16 bcdHID;
	u8 bCountryCode;
	u8 bNumDescriptors;		/* number of descriptors (at least one) */
	u8 bDescriptorType;		/* mandatory descriptor type */
	u16 wDescriptorLength;
} __attribute__((packed)) dhid = { 9, USBCLIENT_DESC_TYPE_HID, 0x0110, 0x0, 1, 0x22, 76 };


usbclient_desc_intf_t diface = { .len = 9, .desc_type = USBCLIENT_DESC_TYPE_INTF, .intf_num = 0, .alt_set = 0,
	.num_endpt = 1, .intf_class = 0x03, .intf_subclass = 0x00, .intf_prot = 0x00, .intf_str = 2
};


usbclient_desc_conf_t dconfig = { .len = 9, .desc_type = USBCLIENT_DESC_TYPE_CFG,
	.total_len = sizeof(usbclient_desc_conf_t) + sizeof(usbclient_desc_intf_t) + sizeof(dhid) + sizeof(usbclient_desc_ep_t),
	.num_intf = 1, .conf_val = 1, .conf_str = 1, .attr_bmp = 0xc0, .max_pow = 10
};


usbclient_desc_dev_t ddev = {
	.len = sizeof(usbclient_desc_dev_t), .desc_type = USBCLIENT_DESC_TYPE_DEV, .bcd_usb = 0x200,
	.dev_class = 0, .dev_subclass = 0, .dev_prot = 0, .max_pkt_sz0 = 64,
	.vend_id = 0x15a2, .prod_id = 0x007d, .bcd_dev = 0x0001,
	.man_str = 0, .prod_str = 0, .sn_str = 0,
	.num_conf = 1
};


usbclient_desc_list_t dev, conf, iface, hid, ep, hidreport;


static usbclient_conf_t config = {
	.endpoint_list = {
		.size = 2,
		.endpoints = {
			{ .type = USBCLIENT_ENDPT_TYPE_CONTROL, .direction = 0 /* for control endpoint it's ignored */},
			{ .type = USBCLIENT_ENDPT_TYPE_INTR, .direction = USBCLIENT_ENDPT_DIR_IN } /* IN interrupt endpoint required for HID */
		}
	},
	.descriptors_head = &dev
};


int hid_recv(int what, char *data, unsigned int len, char **outdata)
{
	int res;
//	sdp_cmd_t *cmd;

	if ((res = usbclient_receive(&config.endpoint_list.endpoints[1], data, len - 1)) < 0)
		return -1;

	if (!what) {
		if (data[0] != 1)	/* HID report SDP CMD */
			return -1;
		//cmd = (sdp_cmd_t *)&data[1];
		//cmd->type = ntohs(cmd->type);
		//cmd->address = ntohl(cmd->address);
		//cmd->len = ntohl(cmd->data_count);
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


int hid_init(int (**rf)(int, char *, unsigned int, char **), int (**sf)(int, const char *, unsigned int))
{
	int res;

	dev.size = 1;
	dev.descriptors = (usbclient_desc_gen_t *)&ddev;;
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
	ep.next = NULL;

	if ((res = usbclient_init(&config)) != EOK) {
		return res;
	}

	*rf = hid_recv;
	*sf = hid_send;
	return EOK;
}

