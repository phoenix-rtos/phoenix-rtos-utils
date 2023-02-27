/*
 * Phoenix-RTOS
 *
 * psd - Serial Download Protocol client
 *
 * Flash Server Manager.
 *
 * Copyright 2019 Phoenix Systems
 * Author: Hubert Buczynski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */


#include "flashmng.h"
#include <imx6ull-flashsrv.h>

#include <sys/mman.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static struct {
	flashsrv_info_t info;

} flashmng_common;

/* jffs2 cleanmarker - write it on clean blocks to mount faster */
struct cleanmarker
{
	uint16_t magic;
	uint16_t type;
	uint32_t len;
};


static const struct cleanmarker oob_cleanmarker =
{
	.magic = 0x1985,
	.type = 0x2003,
	.len = 8
};


int flashmng_isBadBlock(oid_t oid, uint32_t addr)
{
	msg_t msg = { 0 };
	flash_i_devctl_t *idevctl = (flash_i_devctl_t *)msg.i.raw;
	flash_o_devctl_t *odevctl = (flash_o_devctl_t *)msg.o.raw;

	msg.type = mtDevCtl;
	idevctl->type = flashsrv_devctl_isbad;
	idevctl->badblock.oid = oid;
	idevctl->badblock.address = addr;

	if (msgSend(oid.port, &msg) < 0)
		return -1;

	if (odevctl->err != 0)
		return 1;

	return 0;
}


int flashmng_writedev(oid_t oid, uint32_t addr, void *data, int size, int type)
{
	msg_t msg;
	flash_i_devctl_t *idevctl = NULL;
	flash_o_devctl_t *odevctl = NULL;

	msg.type = mtDevCtl;
	msg.i.data = data;
	msg.i.size = size;
	msg.o.data = NULL;
	msg.o.size = 0;

	idevctl = (flash_i_devctl_t *)msg.i.raw;
	idevctl->type = type;
	idevctl->write.oid = oid;
	idevctl->write.size = size;
	idevctl->write.address = addr;

	if (msgSend(oid.port, &msg) < 0)
		return -1;

	odevctl = (flash_o_devctl_t *)msg.o.raw;

	if (odevctl->err != size)
		return -1;

	return 0;
}


int flashmng_eraseBlocks(oid_t oid, unsigned int start, unsigned int size)
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
	idevctl->type = flashsrv_devctl_erase;
	idevctl->erase.oid = oid;
	idevctl->erase.size = size;
	idevctl->erase.address = start;

	if (msgSend(oid.port, &msg) < 0)
		return -1;

	odevctl = (flash_o_devctl_t *)msg.o.raw;

	if (odevctl->err < 0)
		return -1;

	return 0;
}


int flashmng_getAttr(int type, long long *val, oid_t oid)
{
	msg_t msg;

	msg.type = mtGetAttr;
	msg.i.data = NULL;
	msg.i.size = 0;
	msg.o.data = NULL;
	msg.o.size = 0;

	msg.i.attr.type = type;
	msg.i.attr.oid = oid;

	if ((msgSend(oid.port, &msg) < 0) || (msg.o.attr.err < 0))
		return -1;

	*val = msg.o.attr.val;

	return 0;
}


int flashmng_checkRange(oid_t oid, unsigned int start, unsigned int size, dbbt_t **dbbt)
{
	uint32_t *bbt;
	uint32_t bbtn = 0;
	int dbbtsz;
	uint32_t addr;

	bbt = calloc(BB_MAX, sizeof(uint32_t));
	if (bbt == NULL) {
		printf("Failed to alloc memory for BBT");
		return -1;
	}

	for (addr = start; addr < start + size; addr += flashmng_common.info.erasesz) {
		unsigned int blockno = addr / flashmng_common.info.erasesz;

		if (flashmng_isBadBlock(oid, addr)) {
			printf("Block %u is marked as bad\n", blockno);
			bbt[bbtn++] = blockno;
		}

		if (bbtn >= BB_MAX) {
			printf("Too many bad blocks. Flash is not useable\n");
			/* TODO: no -  we need only kernel partitions badblocks in DBBT */
			/* TODO2: we could have more than one page with bad block numbers */
			break;
		}
	}

	printf("Total blocks checked: %u\n", size / flashmng_common.info.erasesz);
	printf("Number of bad blocks:  %u\n", bbtn);
	printf("------------------\n");

	/* FIXME: memory leak if dbbt already allocated */
	if (dbbt != NULL && bbtn < BB_MAX) {
		dbbtsz = (sizeof(dbbt_t) + (sizeof(uint32_t) * bbtn) + flashmng_common.info.writesz - 1) & ~(flashmng_common.info.writesz - 1);
		*dbbt = malloc(dbbtsz);
		memset(*dbbt, 0, dbbtsz);
		memcpy(&((*dbbt)->bad_block), bbt, sizeof(uint32_t) * bbtn);
		(*dbbt)->entries_num = bbtn;
	}

	free(bbt);
	return (bbtn >= BB_MAX ? -1 : 0);
}


/* write JFFS2 clean block markers */
int flashmng_cleanMarkers(oid_t oid, unsigned int start, unsigned int size)
{
	int ret = 0;
	uint32_t addr;

	for (addr = start; addr < start + size; addr += flashmng_common.info.erasesz) {
		unsigned int blockno = addr / flashmng_common.info.erasesz; /* note: this is relative to the beginning of the partition */

		if (flashmng_isBadBlock(oid, addr)) {
			printf("CleanMarkers: block %u is marked as bad - skipping\n", blockno);
			continue;
		}
		ret += flashmng_writedev(oid, addr, (void *)&oob_cleanmarker, sizeof(oob_cleanmarker), flashsrv_devctl_writemeta);
	}

	return ret;
}


int flashmng_getInfo(oid_t oid, flashsrv_info_t *info)
{
	msg_t msg = { 0 };
	flash_i_devctl_t *idevctl = (flash_i_devctl_t *)msg.i.raw;
	flash_o_devctl_t *odevctl = (flash_o_devctl_t *)msg.o.raw;

	msg.type = mtDevCtl;

	idevctl->type = flashsrv_devctl_info;

	if (msgSend(oid.port, &msg) < 0)
		return -1;

	memcpy(&flashmng_common.info, &odevctl->info, sizeof(flashsrv_info_t));
	memcpy(info, &odevctl->info, sizeof(flashsrv_info_t));
	return 0;
}


int flashmng_readraw(oid_t oid, uint32_t addr, void *data, int size)
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
	idevctl->read.address = addr;

	if (msgSend(oid.port, &msg) < 0)
		return -1;

	if (odevctl->err != size)
		return -1;

	return 0;
}
