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

#ifndef _SDP_H_
#define _SDP_H_

#include <stdint.h>

#include <hid_client.h>


/* Addresses definitions for WRITE_REGISTER */
#define CHANGE_PARTITION -1
#define ERASE_ROOTFS_ADDRESS -2
#define ERASE_ALL_ADDRESS -3
#define CHECK_PRODUCTION -4
#define CONTROL_BLOCK_ADDRESS -5
#define BLOW_FUSES -6
#define CLOSE_PSD -100


enum {
	SDP_READ_REGISTER = 0x0101,
	SDP_WRITE_REGISTER = 0x0202,
	SDP_WRITE_FILE = 0x0404,
	SDP_ERROR_STATUS = 0x0505,
	SDP_DCD_WRITE = 0x0a0a,
	SDP_JUMP_ADDRESS = 0x0b0b,
	SDP_DCD_SKIP = 0x0c0c
};


typedef struct _sdp_cmd_t {
	uint16_t type;
	uint32_t address;
	uint8_t format;
	uint32_t datasz;
	uint32_t data;
	uint8_t reserved;
} __attribute__((packed)) sdp_cmd_t;


int sdp_init(const usb_hid_dev_setup_t* dev_setup);


int sdp_send(int report, const char *data, unsigned int len);


int sdp_recv(int report, char *data, unsigned int len, char **outdata);


void sdp_destroy(void);


#endif
