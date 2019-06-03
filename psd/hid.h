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

#ifndef _HID_H_
#define _HID_H_

enum {
	hidOK = 0,
	eReport1,
	eReport2,
	eReport3,
	eReport4,
	eErase,
	eControlBlock
};

extern int hid_init(int (**rf)(int, char *, unsigned int, char **), int (**sf)(int, const char *, unsigned int));


#endif
