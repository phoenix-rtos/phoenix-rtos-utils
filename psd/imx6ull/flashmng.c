/*
 * Phoenix-RTOS
 *
 * psd - Serial Download Protocol client
 *
 * Flash Server Manager.
 *
 * Copyright 2019, 2025 Phoenix Systems
 * Author: Hubert Buczynski, Ziemowit Leszczynski
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

	msg.type = mtDevCtl;
	msg.oid = oid;
	idevctl->type = flashsrv_devctl_isbad;
	idevctl->badblock.address = addr;

	if (msgSend(oid.port, &msg) < 0)
		return -1;

	if (msg.o.err != 0)
		return 1;

	return 0;
}


int flashmng_writedev(oid_t oid, uint32_t addr, void *data, int size, int type)
{
	msg_t msg;
	flash_i_devctl_t *idevctl = NULL;

	msg.type = mtDevCtl;
	msg.i.data = data;
	msg.i.size = size;
	msg.o.data = NULL;
	msg.o.size = 0;
	msg.oid = oid;

	idevctl = (flash_i_devctl_t *)msg.i.raw;
	idevctl->type = type;
	idevctl->write.size = size;
	idevctl->write.address = addr;

	if (msgSend(oid.port, &msg) < 0)
		return -1;

	if (msg.o.err != size)
		return -1;

	return 0;
}


int flashmng_eraseBlocks(oid_t oid, unsigned int start, unsigned int size)
{
	msg_t msg;
	flash_i_devctl_t *idevctl = NULL;

	msg.type = mtDevCtl;
	msg.i.data = NULL;
	msg.i.size = 0;
	msg.o.data = NULL;
	msg.o.size = 0;
	msg.oid = oid;

	idevctl = (flash_i_devctl_t *)msg.i.raw;
	idevctl->type = flashsrv_devctl_erase;
	idevctl->erase.size = size;
	idevctl->erase.address = start;

	if (msgSend(oid.port, &msg) < 0)
		return -1;

	if (msg.o.err < 0)
		return -1;

	return 0;
}


int flashmng_getAttr(int type, long long *val, oid_t oid)
{
	msg_t msg;

	msg.oid = oid;
	msg.type = mtGetAttr;
	msg.i.data = NULL;
	msg.i.size = 0;
	msg.o.data = NULL;
	msg.o.size = 0;

	msg.i.attr.type = type;

	if ((msgSend(oid.port, &msg) < 0) || (msg.o.err < 0)) {
		return -1;
	}
	*val = msg.o.attr.val;

	return 0;
}


int flashmng_checkRange(oid_t oid, unsigned int start, unsigned int size, unsigned int offs, dbbt_t *dbbt)
{
	uint32_t bbcnt = 0;
	uint32_t addr;

	for (addr = start; addr < start + size; addr += flashmng_common.info.erasesz) {
		unsigned int blockno = (offs + addr) / flashmng_common.info.erasesz;

		if (flashmng_isBadBlock(oid, addr)) {
			printf("Block %u is marked as bad\n", blockno);
			dbbt->bad_block[dbbt->entries_num] = blockno;
			dbbt->entries_num++;
			bbcnt++;
		}

		if (dbbt->entries_num >= BCB_BB_MAX) {
			printf("Too many bad blocks. Flash is not useable\n");
			break;
		}
	}

	printf("Total blocks checked: %u\n", addr / flashmng_common.info.erasesz);
	printf("Number of bad blocks:  %u\n", bbcnt);
	printf("------------------\n");

	return (dbbt->entries_num >= BCB_BB_MAX ? -1 : 0);
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

	msg.type = mtDevCtl;
	msg.o.data = data;
	msg.o.size = size;
	msg.oid = oid;

	idevctl->type = flashsrv_devctl_readraw;
	idevctl->read.size = size;
	idevctl->read.address = addr;

	if (msgSend(oid.port, &msg) < 0)
		return -1;

	if (msg.o.err != size)
		return -1;

	return 0;
}
