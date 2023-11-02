/*
 * Phoenix-RTOS
 *
 * psd - Serial Download Protocol client
 *
 * Copyright 2019 Phoenix Systems
 * Author: Bartosz Ciesla
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <sys/mman.h>
#include <sys/threads.h>
#include <errno.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <usbclient.h>
#include <usb.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <sys/list.h>

#define CONTROL_ENDPOINT 0
#define INTERRUPT_ENPOINT 1

#define MAX_REPORT (128)
#define MAX_STRING (128)
#define MAX_RECV_DATA (0x1000)
#define MAX_MODS (8)
#define INIT_PATH "/init"
/* Disable sending report 3 and 4 in response */
#define DISABLE_REPORT_3_4 1

/* Debug print buffer */
extern volatile uint8_t* sprint_buf;
#define print_msg() \
do { \
	if (sprint_buf[0]) { \
		printf("%s", sprint_buf + 1); \
		sprint_buf[0] = 0; \
	} \
} while(0);

#define print_buffer(buffer, size) \
do { \
	for (int i = 0; (i < size) && (i < MAX_RECV_DATA); i++) { \
		printf("%02x ", buffer[i]); \
	} \
	printf("\n"); \
} while(0);

/* Endpoints configuration */

/* Descriptors configuration */
/* HID descriptor */
typedef struct _usbclient_descriptor_hid_t {
	uint8_t	len;
	uint8_t	desc_type;
	uint16_t bcd_hid;
	uint8_t	country_code;
	uint8_t	num_desc;		/* number of descriptors (at least one) */
	uint8_t	desc_type0;		/* mandatory descriptor type */
	uint16_t	desc_len0;		/* mandatory descriptor length */
} __attribute__((packed)) usbclient_descriptor_hid_t;

/* HID report descriptor */
typedef struct _usbclient_descriptor_hid_report_t {
	uint8_t	len;
	uint8_t	desc_type;
	uint8_t	report_data[MAX_REPORT];
} __attribute__((packed)) usbclient_descriptor_hid_report_t;

/* String descriptor */
typedef struct _usbclient_descriptor_string_t {
	uint8_t	len;
	uint8_t	desc_type;
	uint8_t string[MAX_STRING];
} __attribute__((packed)) usbclient_descriptor_string_t;

usb_device_desc_t device_desc = {
	.bLength = sizeof(usb_device_desc_t),
	.bDescriptorType = USB_DESC_DEVICE,
	.bcdUSB = 0x200,
	.bDeviceClass = 0,
	.bDeviceSubClass = 0,
	.bDeviceProtocol = 0,
	.bMaxPacketSize0 = 64,
	.idVendor = 0x15a2,
	.idProduct = 0x007d,
	.bcdDevice = 0x0001,
	.iManufacturer = 0,
	.iProduct = 0,
	.iSerialNumber = 0,
	.bNumConfigurations = 1
};

usb_configuration_desc_t config_desc = {
	.bLength = 9,
	.bDescriptorType = USB_DESC_CONFIG,
	.wTotalLength = sizeof(usb_configuration_desc_t) +
		sizeof(usb_interface_desc_t) +
		sizeof(usbclient_descriptor_hid_t) +
		sizeof(usb_endpoint_desc_t),
	.bNumInterfaces = 1,
	.bConfigurationValue = 1,
	.iConfiguration = 1,
	.bmAttributes = 0xc0,
	.bMaxPower = 10
};

usb_interface_desc_t interface_desc = {
	.bLength = 9,
	.bDescriptorType = USB_DESC_INTERFACE,
	.bInterfaceNumber = 0,
	.bAlternateSetting = 0,
	.bNumEndpoints = 1,
	.bInterfaceClass = 0x03,
	.bInterfaceSubClass = 0x00,
	.bInterfaceProtocol = 0x00,
	.iInterface = 2
};

usbclient_descriptor_hid_report_t hid_report_desc = {
	.len = 2 + 76,
	.desc_type = USB_DESC_TYPE_HID_REPORT,
	/* Raw HID report descriptor - compatibile with IMX6ULL SDP protocol */
	.report_data = {
		0x06, 0x00, 0xff, 0x09, 0x01, 0xa1, 0x01, 0x85, 0x01, 0x19,
		0x01, 0x29, 0x01, 0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08,
		0x95, 0x10, 0x91, 0x02, 0x85, 0x02, 0x19, 0x01, 0x29, 0x01,
		0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x80, 0x95, 0x40, 0x91,
		0x02, 0x85, 0x03, 0x19, 0x01, 0x29, 0x01, 0x15, 0x00, 0x26,
		0xff, 0x00, 0x75, 0x08, 0x95, 0x04, 0x81, 0x02, 0x85, 0x04,
		0x19, 0x01, 0x29, 0x01, 0x15, 0x00, 0x26, 0xff, 0x00, 0x75,
		0x08, 0x95, 0x40, 0x81, 0x02, 0xc0 }
};

usbclient_descriptor_hid_t hid_desc = {
	.len = sizeof(usbclient_descriptor_hid_t),
	.desc_type = USB_DESC_TYPE_HID,
	.bcd_hid = 0x0110,
	.country_code = 0x0,
	.num_desc = 1,
	.desc_type0 = 0x22,
	.desc_len0 = 76
};

usb_endpoint_desc_t endpoint_desc = {
	.bLength = 7,
	.bDescriptorType = USB_DESC_ENDPOINT,
	.bEndpointAddress = 0x81, /* direction IN */
	.bmAttributes = 0x03,	/* interrupt transfer */
	.wMaxPacketSize = 64,
	.bInterval = 0x01
};

usb_string_desc_t string_zero_desc = {
	.bLength = 4,
	.bDescriptorType = USB_DESC_STRING,
	.wData = {0x04, 0x09} /* English */
};

usb_string_desc_t string_man_desc = {
	.bLength = 27 * 2 + 2,
	.bDescriptorType = USB_DESC_STRING,
	.wData = { 'F', 0, 'r', 0, 'e', 0, 'e', 0, 's', 0, 'c', 0, 'a', 0, 'l', 0, 'e', 0, ' ', 0, 'S', 0, 'e', 0, 'm', 0, 'i', 0, 'c', 0, 'o', 0, 'n', 0, 'd', 0, 'u', 0, 'c', 0, 't', 0, 'o', 0, 'r', 0, ' ', 0, 'I', 0, 'n', 0, 'c', 0 }
};

usb_string_desc_t string_prod_desc = {
	.bLength = 13 * 2 + 2,
	.bDescriptorType = USB_DESC_STRING,
	.wData = { 'S', 0, 'E', 0, ' ', 0, 'B', 0, 'l', 0, 'a', 0, 'n', 0, 'k', 0, ' ', 0, '6', 0, 'U', 0, 'L', 0, 'L', 0 }
};


struct {
	usb_desc_list_t *descList;

	usb_desc_list_t dev;
	usb_desc_list_t conf;
	usb_desc_list_t iface;

	usb_desc_list_t hid;
	usb_desc_list_t ep;

	usb_desc_list_t str0;
	usb_desc_list_t strman;
	usb_desc_list_t strprod;

	usb_desc_list_t hidreport;
} hid_common;


/* SDP command */
typedef struct _sdp_command {
	uint16_t type;
	uint32_t address;
	uint8_t format;
	uint32_t data_count;
	uint32_t data;
	uint8_t _reserved;
} __attribute__((packed)) sdp_command_t;

enum {
	HID_REPORT_SDP_COMMAND = 1,
	HID_REPORT_SDP_COMMAND_DATA,
	HID_REPORT_HAB_SECURITY,
	HID_REPORT_SDP_RESPONSE_DATA,
};

enum {
	SDP_CMD_READ_REG = 0x0101,
	SDP_CMD_WRITE_REG = 0x0202,
	SDP_CMD_WRITE_FILE = 0x0404,
	SDP_CMD_ERR_STATUS = 0x0505,
	SDP_CMD_DCD_WRITE = 0x0a0a,
	SDP_CMD_JMP_ADDR = 0x0b0b,
	SDP_CMD_SKIP_DCD_HEADER = 0x0c0c
};

typedef struct _mod_t
{
	size_t size;
	char name[MAX_STRING];
	char args[MAX_STRING];
	void *data;
} mod_t;

/* Array holding received data */
static mod_t mods[MAX_MODS];

void print_sdp_command(sdp_command_t *cmd)
{
	printf("SDP command\n    type: 0x%04x\n    address: 0x%08x\n    format: 0x%02x\n    data_count: %d\n    data: 0x%08x\n",
			cmd->type,
			cmd->address,
			cmd->format,
			cmd->data_count,
			cmd->data);
}

int send_hab_security(void)
{
	//const uint8_t send_data[] = { HID_REPORT_HAB_SECURITY, 0x12, 0x34, 0x34, 0x12 };
	const uint8_t send_data[] = { HID_REPORT_HAB_SECURITY, 0x56, 0x78, 0x78, 0x56 };
	return usbclient_send(INTERRUPT_ENPOINT, send_data, sizeof(send_data));
}

int send_complete_status(void)
{
	const uint8_t send_data[] = { HID_REPORT_SDP_RESPONSE_DATA, 0x88, 0x88, 0x88, 0x88 };
	return usbclient_send(INTERRUPT_ENPOINT, send_data, sizeof(send_data));
}

void decode_sdp_command(sdp_command_t *cmd, uint8_t *data, uint32_t len)
{
	cmd->type = ntohs(*((uint16_t*)data));
	data += 2;
	cmd->address = ntohl(*((uint32_t*)data));
	data += 4;
	cmd->format = *data;
	data += 1;
	cmd->data_count = ntohl(*((uint32_t*)data));
	data += 4;
	cmd->_reserved = *data;
}

int decode_incoming_report(sdp_command_t *cmd, uint8_t *data, uint32_t len)
{
	if (len <= 1)
		return -1;

	const uint8_t report_type = data[0];
	switch (report_type){
		case HID_REPORT_SDP_COMMAND:
			decode_sdp_command(cmd, data + 1, len - 1);
			return report_type;
		case HID_REPORT_SDP_COMMAND_DATA:
			return report_type;
		default:
			printf("Invalid report type (0x%02x)\n", report_type);
			break;
	}

	return -1;
}


int receive_name(mod_t *mod)
{
	/* Structure holding received data info */
	sdp_command_t command;
	uint8_t recv_data[MAX_RECV_DATA] = { 0 };

	/* Receive command */
	int32_t result = usbclient_receive(CONTROL_ENDPOINT, recv_data, MAX_RECV_DATA);
	if (decode_incoming_report(&command, recv_data, result) != HID_REPORT_SDP_COMMAND ||
			command.type != SDP_CMD_WRITE_FILE) {
		printf("Did not receive proper command. Exiting...\n");
		return -1;
	}

	/* Command with size 0 finishes downloading */
	if (command.data_count == 0) {
		printf("Download finished\n");
		return 1;
	}

	result = usbclient_receive(CONTROL_ENDPOINT, recv_data, MAX_RECV_DATA);
	if (recv_data[0] == HID_REPORT_SDP_COMMAND_DATA) {
		int to_copy = result - 1 > MAX_STRING ? MAX_STRING - 1 : result - 1;
		memcpy(mod->name, recv_data + 1, to_copy);
		mod->name[MAX_STRING - 1] = 0;
	} else {
		printf("name: Invalid report type (0x%02x)\n", recv_data[0]);
		return -1;
	}
	printf("Receiving '%s'\n", mod->name);
	return 0;
}


int receive_args(mod_t *mod)
{
	/* Structure holding received data info */
	sdp_command_t command;
	uint8_t recv_data[MAX_RECV_DATA] = { 0 };

	/* Receive command */
	int32_t result = usbclient_receive(CONTROL_ENDPOINT, recv_data, MAX_RECV_DATA);
	if (decode_incoming_report(&command, recv_data, result) != HID_REPORT_SDP_COMMAND ||
			command.type != SDP_CMD_WRITE_FILE) {
		printf("Did not receive proper command. Exiting...\n");
		return -1;
	}

	/* Do not expect second report */
	if (command.data_count == 0) {
		mod->args[0] = 0;
		return 0;
	}

	result = usbclient_receive(CONTROL_ENDPOINT, recv_data, MAX_RECV_DATA);
	if (recv_data[0] == HID_REPORT_SDP_COMMAND_DATA) {
		int to_copy = result - 1 > MAX_STRING ? MAX_STRING - 1 : result - 1;
		memcpy(mod->args, recv_data + 1, to_copy);
		mod->args[MAX_STRING - 1] = 0;
	} else {
		printf("args: Invalid report type (0x%02x)\n", recv_data[0]);
		return -1;
	}
	printf("Arguments '%s'\n", mod->args);
	return 0;
}


int receive_content(mod_t *mod)
{
	/* Structure holding received data info */
	sdp_command_t command;
	uint8_t recv_data[MAX_RECV_DATA] = { 0 };
	/* Receive command */
	int32_t result = usbclient_receive(CONTROL_ENDPOINT, recv_data, MAX_RECV_DATA);
	if (decode_incoming_report(&command, recv_data, result) != HID_REPORT_SDP_COMMAND ||
			command.type != SDP_CMD_WRITE_FILE) {
		printf("Did not receive proper command. Exiting...\n");
		return -1;
	}

	mod->size = command.data_count;
	/* Allocate memory for module - not portable */
	mod->data = mmap(NULL, (mod->size + 0xfff) & ~0xfff, PROT_WRITE | PROT_READ, MAP_UNCACHED | MAP_ANONYMOUS, -1, 0);

	/* Read modules */
	uint32_t read_data = 0;
	printf("Reading module\n");
	while (read_data < mod->size) {
		/* Receive command */
		result = usbclient_receive(CONTROL_ENDPOINT, recv_data, MAX_RECV_DATA);
		if (recv_data[0] == HID_REPORT_SDP_COMMAND_DATA) {
			memcpy(mod->data + read_data, recv_data + 1, result - 1);
			read_data += result - 1; /* substract 1 byte for report ID */
		} else {
			printf("content: Invalid report type (0x%02x)\n", recv_data[0]);
			return -1;
		}
	};
	return 0;
}


int main(int argc, char **argv)
{
	printf("Started psd\n");

	/* Initialize descriptor list */
	hid_common.dev.descriptor = (usb_functional_desc_t *)&device_desc;
	LIST_ADD(&hid_common.descList, (usb_desc_list_t *)&hid_common.dev);

	hid_common.conf.descriptor = (usb_functional_desc_t *)&config_desc;
	LIST_ADD(&hid_common.descList, (usb_desc_list_t *)&hid_common.conf);

	hid_common.iface.descriptor = (usb_functional_desc_t *)&interface_desc;
	LIST_ADD(&hid_common.descList, &hid_common.iface);

	hid_common.hid.descriptor = (usb_functional_desc_t *)&hid_desc;
	LIST_ADD(&hid_common.descList, &hid_common.hid);

	hid_common.ep.descriptor = (usb_functional_desc_t *)&endpoint_desc;
	LIST_ADD(&hid_common.descList, &hid_common.ep);

	hid_common.str0.descriptor = (usb_functional_desc_t *)&string_zero_desc;
	LIST_ADD(&hid_common.descList, &hid_common.str0);

	hid_common.strman.descriptor = (usb_functional_desc_t *)&string_man_desc;
	LIST_ADD(&hid_common.descList, &hid_common.strman);

	hid_common.strprod.descriptor = (usb_functional_desc_t *)&string_prod_desc;
	LIST_ADD(&hid_common.descList, &hid_common.strprod);

	hid_common.hidreport.descriptor = (usb_functional_desc_t *)&hid_report_desc;
	LIST_ADD(&hid_common.descList, &hid_common.hidreport);

	hid_common.hidreport.next = NULL;


	/* Initialize USB library */
	int32_t result = 0;
	if((result = usbclient_init(hid_common.descList)) != EOK) {
		printf("Couldn't initialize USB library (%d)\n", result);
	}
	printf("Initialized USB library\n");

	uint32_t modn = 0;
	while (1) {
		if (modn >= MAX_MODS) {
			printf("Maximum modules number reached (%d). Stopping USB\n", MAX_MODS);
			break;
		}

		/* Returns 1 when received finish command */
		if ((result = receive_name(&mods[modn])) != 0) {
			break;
		}
		if ((result = receive_args(&mods[modn])) != 0) {
			break;
		}
		if ((result = receive_content(&mods[modn++])) != 0) {
			break;
		}

		printf("Finished reading module\n");

#ifndef DISABLE_REPORT_3_4
		/* Response HAB security */
		send_hab_security();
		/* Response complete status */
		send_complete_status();
#endif
	}

	/* Cleanup all USB related data */
	usbclient_destroy();

	/* Write data to flash */
	/* TODO: switch to raw flash writes instead of using filesystem */
	if (result == 1) {
		if (mkdir("/init", 0777) < 0 && errno != EEXIST) {
			printf("Couldn't create directory\n");
			return -1;
		}

		for(int i = 0; i < modn; i++) {
			printf("Writing module (%d/%d)\n", i + 1, modn);
			char path[MAX_STRING] = { INIT_PATH"/" };
			strcat(path, mods[i].name + 1);
			FILE* file = fopen(path, "w");
			fwrite(mods[i].data, 1, mods[i].size, file);
			fclose(file);
		}

		/* Execute nandtool module */
		/* It is expected that only one module has X in name and it's nandtool */
		/* This solution will be replaced with raw flash writes */
		char *arg_tok;
		char *argv[16] = { 0 };
		int argc;
		for (int i = 0; i < modn; i++) {
			if(mods[i].name[0] == 'X') {
				char path[MAX_STRING] = { INIT_PATH"/" };
				argc = 1;
				arg_tok = strtok(mods[i].args, ",");

				while (arg_tok != NULL && argc < 15){
					argv[argc] = arg_tok;
					arg_tok = strtok(NULL, ",");
					argc++;
				}
				argv[argc] = NULL;
				strcat(path, mods[i].name + 1);

				argv[0] = path;
				if(execve(path, argv, NULL) != EOK) {
					printf("Failed to start %s\n", path);
				}
				break;
			}
		}
	}

	printf("Exiting\n");
	return EOK;
}
