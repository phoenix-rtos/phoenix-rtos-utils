/*
 * Phoenix-RTOS
 *
 * psd - Serial Download Protocol client
 *
 * Copyright 2019 Phoenix Systems
 * Author: Hubert Buczynski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include "sdp.h"

#include <unistd.h>
#include <arpa/inet.h>

#define CONTROL_ENDPOINT 0
#define INTERRUPT_ENPOINT 1

int sdp_init(const usb_hid_dev_setup_t *dev_setup)
{
	return hid_init(dev_setup);
}


int sdp_send(int report, const char *data, unsigned int len)
{
	if (report == 3) {	/* HAB security configuration */
		if (data[0] != 3 || len != 5)
			return -1;
	} else if (report == 4) {	/* SDP command response data */
		if (data[0] != 4 || len > 65)
			return -2;
	}

	return hid_send(INTERRUPT_ENPOINT, data, len);
}


int sdp_recv(int report, char *data, unsigned int len, char **outdata)
{
	int res;
	sdp_cmd_t *cmd;

	if ((res = hid_recv(CONTROL_ENDPOINT, data, len)) < 0)
		return -1;

	if (!report) {
		if (data[0] != 1)       /* HID report SDP CMD */
			return -1;

		cmd = (sdp_cmd_t *)&data[1];
		cmd->address = ntohl(cmd->address);
		cmd->datasz = ntohl(cmd->datasz);
		cmd->data = ntohl(cmd->data);
	}
	else if (data[0] != 2) {	/* HID report SDP CMD DATA */
		return -2;
	}

	*outdata = data + 1;

	return res - 1;
}


void sdp_destroy(void)
{
	hid_destroy();
}

