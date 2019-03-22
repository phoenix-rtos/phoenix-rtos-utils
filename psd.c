#include <sys/mman.h>
#include <sys/threads.h>
#include <errno.h>
#include <stdio.h>
#include <usbclient.h>

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

#define MAX_STRING (128)
/* String descriptor */
typedef struct _usbclient_descriptor_string_t {
	uint8_t	len;
	uint8_t	desc_type;
	uint8_t string[MAX_STRING];
} __attribute__((packed)) usbclient_descriptor_string_t;

usbclient_descriptor_device_t device_desc = {
	.len = sizeof(usbclient_descriptor_device_t),
	.desc_type = USBCLIENT_DESC_TYPE_DEV,
	.bcd_usb = 0x200,
	.dev_class = 0,
	.dev_subclass = 0,
	.dev_prot = 0,
	.max_pkt_sz0 = 64,
	.vend_id = 0x15a2,
	.prod_id = 0x007d,
	.bcd_dev = 0x0001,
	.man_str = 0,
	.prod_str = 0,
	.sn_str = 0,
	.num_conf = 1
};

usbclient_descriptor_configuration_t config_desc = {
	.len = 9,
	.desc_type = USBCLIENT_DESC_TYPE_CFG,
	.total_len = sizeof(usbclient_descriptor_configuration_t) +
		sizeof(usbclient_descriptor_interface_t) +
		sizeof(usbclient_descriptor_hid_t) +
		sizeof(usbclient_descriptor_endpoint_t),
	.num_intf = 1,
	.conf_val = 1,
	.conf_str = 1,
	.attr_bmp = 0xc0,
	.max_pow = 10
};

usbclient_descriptor_interface_t interface_desc = {
	.len = 9,
	.desc_type = USBCLIENT_DESC_TYPE_INTF,
	.intf_num = 0,
	.alt_set = 0,
	.num_endpt = 1,
	.intf_class = 0x03,
	.intf_subclass = 0x00,
	.intf_prot = 0x00,
	.intf_str = 2
};

/* Raw HID report descriptor - compatibile with IMX6ULL SDP protocol */
uint8_t report_desc[] = {
	0x06, 0x00, 0xff, 0x09, 0x01, 0xa1, 0x01, 0x85, 0x01, 0x19,
	0x01, 0x29, 0x01, 0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08,
	0x95, 0x10, 0x91, 0x02, 0x85, 0x02, 0x19, 0x01, 0x29, 0x01,
	0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x80, 0x95, 0x40, 0x91,
	0x02, 0x85, 0x03, 0x19, 0x01, 0x29, 0x01, 0x15, 0x00, 0x26,
	0xff, 0x00, 0x75, 0x08, 0x95, 0x04, 0x81, 0x02, 0x85, 0x04,
	0x19, 0x01, 0x29, 0x01, 0x15, 0x00, 0x26, 0xff, 0x00, 0x75,
	0x08, 0x95, 0x40, 0x81, 0x02, 0xc0 };

usbclient_descriptor_hid_t hid_desc = {
	.len = sizeof(usbclient_descriptor_hid_t),
	.desc_type = USBCLIENT_DESC_TYPE_HID,
	.bcd_hid = 0x0110,
	.country_code = 0x0,
	.num_desc = 1,
	.desc_type0 = 0x22,
	.desc_len0 = sizeof(report_desc)
};

usbclient_descriptor_endpoint_t endpoint_desc = {
	.len = 7,
	.desc_type = USBCLIENT_DESC_TYPE_ENDPT,
	.endpt_addr = 0x81, /* direction IN */
	.attr_bmp = 0x03,	/* interrupt transfer */
	.max_pkt_sz = 64,
	.interval = 0x01
};

usbclient_descriptor_string_zero_t string_zero_desc = {
	.len = sizeof(usbclient_descriptor_string_zero_t),
	.desc_type = USBCLIENT_DESC_TYPE_STR,
	.w_langid0 = 0x0409 /* English */
};

usbclient_descriptor_string_t string_man_desc = {
	.len = 27 * 2 + 2,
	.desc_type = USBCLIENT_DESC_TYPE_STR,
	.string = { 'F', 0, 'r', 0, 'e', 0, 'e', 0, 's', 0, 'c', 0, 'a', 0, 'l', 0, 'e', 0, ' ', 0, 'S', 0, 'e', 0, 'm', 0, 'i', 0, 'c', 0, 'o', 0, 'n', 0, 'd', 0, 'u', 0, 'c', 0, 't', 0, 'o', 0, 'r', 0, ' ', 0, 'I', 0, 'n', 0, 'c', 0 }
};

usbclient_descriptor_string_t string_prod_desc = {
	.len = 13 * 2 + 2,
	.desc_type = USBCLIENT_DESC_TYPE_STR,
	.string = { 'S', 0, 'E', 0, ' ', 0, 'B', 0, 'l', 0, 'a', 0, 'n', 0, 'k', 0, ' ', 0, '6', 0, 'U', 0, 'L', 0, 'L', 0 }
};


usbclient_descriptor_list_t string_prod_el = { .size = 1, .descriptors = (usbclient_descriptor_generic_t*)&string_prod_desc, .next = NULL };
usbclient_descriptor_list_t string_man_el = { .size = 1, .descriptors = (usbclient_descriptor_generic_t*)&string_man_desc, .next = &string_prod_el };
usbclient_descriptor_list_t string_zero_el = { .size = 1, .descriptors = (usbclient_descriptor_generic_t*)&string_zero_desc, .next = &string_man_el };
usbclient_descriptor_list_t endpoint_el = { .size = 1, .descriptors = (usbclient_descriptor_generic_t*)&endpoint_desc, .next = &string_zero_el };
usbclient_descriptor_list_t hid_el = { .size = 1, .descriptors = (usbclient_descriptor_generic_t*)&hid_desc, .next = &endpoint_el };
usbclient_descriptor_list_t interface_el = { .size = 1, .descriptors = (usbclient_descriptor_generic_t*)&interface_desc, .next = &hid_el };
usbclient_descriptor_list_t config_el = { .size = 1, .descriptors = (usbclient_descriptor_generic_t*)&config_desc, .next = &interface_el };
usbclient_descriptor_list_t device_el = { .size = 1, .descriptors = (usbclient_descriptor_generic_t*)&device_desc , .next = &config_el };

static usbclient_config_t config = {
	.endpoint_list = {
		.size = 2,
		.endpoints = {
			{ .type = USBCLIENT_ENDPT_TYPE_CONTROL, .direction = 0 /* for control endpoint it's ignored */},
			{ .type = USBCLIENT_ENDPT_TYPE_INTR, .direction = USBCLIENT_ENDPT_DIR_IN } /* IN interrupt endpoint required for HID */
		}
	},
	.descriptors_head = &device_el
};

#define MAX_RECV_DATA (1024)

extern volatile uint8_t* sprint_buf;
#define print_msg() \
do { \
	if (sprint_buf[0]) { \
		printf("%s", sprint_buf + 1); \
		sprint_buf[0] = 0; \
	} \
} while(0);

int main(int argc, char **argv)
{
	uint8_t recv_data[MAX_RECV_DATA] = { 0 };
	printf("Started psd\n");

	// Initialize USB library
	int32_t result = 0;
	if((result = usbclient_init(&config)) != EOK) {
		printf("Couldn't initialize USB library (%d)\n", result);
	}

	while(1) {
		print_msg();
		//printf("receive data...\n");
		//int result = usbclient_receive_data(&config.endpoint_list.endpoints[1], recv_data, MAX_RECV_DATA);
		//printf("receive result: %d\n", result);
		//for (int i = 0; (i < result) && i < MAX_RECV_DATA; i++) {
		//	printf("%02x ", recv_data[i]);
		//}
		//printf("\n");
	};

	// Cleanup all USB related data
	usbclient_destroy();

	return EOK;
}
