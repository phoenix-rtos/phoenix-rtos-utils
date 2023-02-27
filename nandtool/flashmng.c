/*
 * Phoenix-RTOS
 *
 * NANDtool utility
 *
 * Flash Server Manager
 *
 * Copyright 2019, 2021 Phoenix Systems
 * Author: Hubert Buczynski, Lukasz Kosinski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/file.h>
#include <sys/msg.h>

#include "flashmng.h"


static struct {
	oid_t oid;
	flashsrv_info_t info;
} flashmng_common;


/* jffs2 cleanmarker - write it on clean erase blocks OOB data to mount faster */
static struct cleanmarker {
	uint16_t magic;
	uint16_t type;
	uint32_t len;
} oob_cleanmarker = {
	.magic = 0x1985,
	.type = 0x2003,
	.len = 8
};


int flashmng_readraw(oid_t oid, offs_t addr, void *data, size_t size)
{
	msg_t msg = { 0 };
	flash_i_devctl_t *idevctl = (flash_i_devctl_t *)msg.i.raw;
	flash_o_devctl_t *odevctl = (flash_o_devctl_t *)msg.o.raw;

	msg.type = mtDevCtl;
	msg.o.data = data;
	msg.o.size = size;

	idevctl->type = flashsrv_devctl_readraw;
	idevctl->read.oid = oid;
	idevctl->read.size = size;
	/* FIXME: Now imx6ull nand DevCtl API supports only 32-bit offsets */
	idevctl->read.address = (uint32_t)addr;

	if (msgSend(oid.port, &msg) < 0) {
		return -1;
	}

	return odevctl->err;
}


static int write_ex(oid_t oid, uint32_t addr, const void *data, size_t size, int type)
{
	msg_t msg = { 0 };
	flash_i_devctl_t *idevctl = (flash_i_devctl_t *)msg.i.raw;
	flash_o_devctl_t *odevctl = (flash_o_devctl_t *)msg.o.raw;
	int err;

	msg.type = mtDevCtl;
	msg.i.data = (void *)data; /* TODO: fix after msg.i.data becomes const void * */
	msg.i.size = size;
	idevctl->type = type;
	idevctl->write.oid = oid;
	idevctl->write.address = addr;
	idevctl->write.size = size;

	if (((err = msgSend(oid.port, &msg)) < 0) || ((err = odevctl->err) < 0))
		return err;

	if (err != size)
		return -EIO;

	return EOK;
}


static int writemeta(oid_t oid, uint32_t addr, const void *data, size_t size)
{
	return write_ex(oid, addr, data, size, flashsrv_devctl_writemeta);
}


int flashmng_writeraw(oid_t oid, unsigned int page, const void *data, size_t size)
{
	flashsrv_info_t *info = flashmng_info(oid);

	if (info == NULL)
		return -EFAULT;

	return write_ex(oid, page * (info->metasz + info->writesz), data, size, flashsrv_devctl_writeraw);
}


int flashmng_erase(oid_t oid, unsigned int start, unsigned int size)
{
	msg_t msg = { 0 };
	flash_i_devctl_t *idevctl = (flash_i_devctl_t *)msg.i.raw;
	flash_o_devctl_t *odevctl = (flash_o_devctl_t *)msg.o.raw;
	flashsrv_info_t *info;
	int err;

	if ((info = flashmng_info(oid)) == NULL)
		return -EFAULT;

	msg.type = mtDevCtl;
	idevctl->type = flashsrv_devctl_erase;
	idevctl->erase.oid = oid;
	idevctl->erase.address = start * info->erasesz;
	idevctl->erase.size = size * info->erasesz;

	if (((err = msgSend(oid.port, &msg)) < 0) || ((err = odevctl->err) < 0))
		return err;

	return EOK;
}


/* write JFFS2 clean block markers */
int flashmng_cleanMarkers(oid_t oid, unsigned int start, unsigned int size)
{
	int ret = 0;
	unsigned int blockno;
	flashsrv_info_t *info = flashmng_info(oid);

	if (info == NULL)
		return -EFAULT;

	for (blockno = start; blockno < start + size; ++blockno) {
		if (flashmng_isbad(oid, blockno)) {
			printf("cleanMarkers: block %u is marked as bad - skipping\n", blockno);
			continue;
		}
		ret += writemeta(oid, blockno * info->erasesz, (void *)&oob_cleanmarker, sizeof(oob_cleanmarker));
	}

	return ret;
}


int flashmng_checkbad(oid_t oid)
{
	flashsrv_info_t *info;
	unsigned int block, nbad = 0;
	int err;

	if ((info = flashmng_info(oid)) == NULL)
		return -EFAULT;

	for (block = 0; block < info->size / info->erasesz; block++) {
		if ((err = flashmng_isbad(oid, block))) {
			if (err < 0)
				return err;

			printf("Block %u is marked as bad\n", block);
			nbad++;
		}
	}

	printf("Total blocks checked: %u\n", block);
	printf("Number of bad blocks:  %u\n", nbad);

	if (nbad >= 256)
		printf("Too many bad blocks. Flash is not usable\n");

	return EOK;
}


int flashmng_isbad(oid_t oid, unsigned int block)
{
	msg_t msg = { 0 };
	flash_i_devctl_t *idevctl = (flash_i_devctl_t *)msg.i.raw;
	flash_o_devctl_t *odevctl = (flash_o_devctl_t *)msg.o.raw;
	flashsrv_info_t *info;
	int err;

	if ((info = flashmng_info(oid)) == NULL)
		return -EFAULT;

	msg.type = mtDevCtl;
	idevctl->type = flashsrv_devctl_isbad;
	idevctl->badblock.oid = oid;
	idevctl->badblock.address = block * info->erasesz;

	if (((err = msgSend(oid.port, &msg)) < 0) || ((err = odevctl->err) < 0))
		return err;

	return odevctl->err;
}


flashsrv_info_t *flashmng_info(oid_t oid)
{
	msg_t msg = { 0 };
	flash_i_devctl_t *idevctl = (flash_i_devctl_t *)msg.i.raw;
	flash_o_devctl_t *odevctl = (flash_o_devctl_t *)msg.o.raw;

	if ((flashmng_common.oid.port == oid.port) && (flashmng_common.oid.id == oid.id))
		return &flashmng_common.info;

	msg.type = mtDevCtl;
	idevctl->type = flashsrv_devctl_info;

	if ((msgSend(oid.port, &msg) < 0) || (odevctl->err < 0))
		return NULL;

	flashmng_common.info.metasz = odevctl->info.metasz;
	flashmng_common.info.writesz = odevctl->info.writesz;
	flashmng_common.info.erasesz = odevctl->info.erasesz;
	flashmng_common.info.size = odevctl->info.size;

	msg.type = mtGetAttr;
	msg.i.attr.type = atSize;
	msg.i.attr.oid = oid;

	if ((msgSend(oid.port, &msg) < 0) || (msg.o.attr.err < 0)) {
		flashmng_common.oid.port = 0;
		flashmng_common.oid.id = 0;
		return NULL;
	}

	flashmng_common.info.size = msg.o.attr.val;
	flashmng_common.oid.port = oid.port;
	flashmng_common.oid.id = oid.id;

	return &flashmng_common.info;
}
