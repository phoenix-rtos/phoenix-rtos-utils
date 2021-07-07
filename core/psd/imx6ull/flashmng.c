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


int flashmng_readraw(oid_t oid, uint32_t paddr, void *data, int size)
{
	msg_t msg;
	flash_i_devctl_t *idevctl = NULL;
	flash_o_devctl_t *odevctl = NULL;

	msg.type = mtDevCtl;
	msg.i.data = NULL;
	msg.i.size = 0;
	msg.o.data = data;
	msg.o.size = size;

	idevctl = (flash_i_devctl_t *)msg.i.raw;
	idevctl->type = flashsrv_devctl_readraw;
	idevctl->readraw.oid = oid;
	idevctl->readraw.size = size;
	idevctl->readraw.address = paddr;

	odevctl = (flash_o_devctl_t *)msg.o.raw;

	if (msgSend(oid.port, &msg) < 0)
		return -1;

	if (odevctl->err < 0)
		return -1;

	return 0;
}


int flashmng_writedev(oid_t oid, uint32_t paddr, void *data, int size, int type)
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
	idevctl->write.address = paddr;

	if (msgSend(oid.port, &msg) < 0)
		return -1;

	odevctl = (flash_o_devctl_t *)msg.o.raw;

	if (odevctl->err < 0)
		return -1;

	return 0;
}


/* start and end are in "erase blocks" */
int flashmng_eraseBlock(oid_t oid, int start, int end, int chip_erase)
{
	msg_t msg;
	flash_i_devctl_t *idevctl = NULL;
	flash_o_devctl_t *odevctl = NULL;

	if (end - start < 0)
		return -1;

	msg.type = mtDevCtl;
	msg.i.data = NULL;
	msg.i.size = 0;
	msg.o.data = NULL;
	msg.o.size = 0;

	idevctl = (flash_i_devctl_t *)msg.i.raw;
	idevctl->type = chip_erase ? flashsrv_devctl_chiperase : flashsrv_devctl_erase;
	idevctl->erase.oid = oid;
	idevctl->erase.size = (end - start) * ERASE_BLOCK_SIZE;
	idevctl->erase.offset = start * ERASE_BLOCK_SIZE;

	if (msgSend(oid.port, &msg) < 0)
		return -1;

	odevctl = (flash_o_devctl_t *)msg.o.raw;

	if (odevctl->err < 0)
		return -1;

	return 0;
}


int flashmng_getAttr(int type, offs_t* val, oid_t oid)
{
	msg_t msg;

	msg.type = mtGetAttr;
	msg.i.data = NULL;
	msg.i.size = 0;
	msg.o.data = NULL;
	msg.o.size = 0;

	msg.i.attr.type = type;
	msg.i.attr.oid = oid;

	if (msgSend(oid.port, &msg) < 0)
		return -1;

	*val = msg.o.attr.val;

	return 0;
}


static int flashmng_checkBlock(char* raw_block)
{
	if (raw_block[FLASH_PAGE_SIZE] != 0xff)
		return 1;

	return 0;
}


int flashmng_checkRange(oid_t oid, int start, int end, dbbt_t **dbbt)
{
	int i, ret = 0;
	int bad = 0;
	int err = 0;
	uint32_t *bbt;
	uint32_t bbtn = 0;
	int dbbtsz;
	void *raw_data;

	bbt = calloc(BB_MAX, sizeof(uint32_t));
	if (bbt == NULL) {
		printf("Failed to map pages from OC RAM\n");
		return -1;
	}

	raw_data = malloc(2 * FLASH_PAGE_SIZE);
	if (raw_data == NULL) {
		printf("Failed to map pages from OC RAM\n");
		free(bbt);
		return -1;
	}

	for (i = start; i < end; i++) {

		memset(raw_data, 0, RAW_FLASH_PAGE_SIZE);
		ret = flashmng_readraw(oid, (i * PAGES_PER_BLOCK * RAW_FLASH_PAGE_SIZE), raw_data, RAW_FLASH_PAGE_SIZE);

		if (ret != EOK) {
			printf("Reading block %d returned an error\n", i);
			err++;
			bbt[bbtn++] = i;
		}

		if (flashmng_checkBlock(raw_data)) {
			printf("Block %d is marked as bad\n", i);
			bad++;
			bbt[bbtn++] = i;
		}

		if (bbtn >= BB_MAX){
			printf("Too many bad blocks. Flash is not usable\n");
			break;
		}
	}

	printf("Total blocks read: %d\n", i);
	printf("Number of read errors: %d\n", err);
	printf("Number of bad blocks:  %d\n", bad);
	printf("------------------\n");

	/* FIXME: memory leak if dbbt already allocated */
	if (dbbt != NULL && bbtn < BB_MAX) {
		dbbtsz = (sizeof(dbbt_t) + (sizeof(uint32_t) * bbtn) + FLASH_PAGE_SIZE - 1) & ~(FLASH_PAGE_SIZE - 1);
		*dbbt = malloc(dbbtsz);
		memset(*dbbt, 0, dbbtsz);
		memcpy(&((*dbbt)->bad_block), bbt, sizeof(uint32_t) * bbtn);
		(*dbbt)->entries_num = bbtn;
	}

	free(bbt);
	free(raw_data);
	return (bbtn >= BB_MAX ? -1 : 0);
}


int flashmng_cleanMarkers(oid_t oid, int start, int end)
{
	void *metabuf;
	int i, ret = 0;

	metabuf = mmap(NULL, FLASH_PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_UNCACHED, OID_NULL, 0);

	if (metabuf == MAP_FAILED)
		return -1;

	memset(metabuf, 0xff, FLASH_PAGE_SIZE);
	memcpy(metabuf, &oob_cleanmarker, 8);

	for (i = start; i < end; i++) {
		ret += flashmng_writedev(oid, (i * PAGES_PER_BLOCK * FLASH_PAGE_SIZE), metabuf, FLASH_PAGE_SIZE, flashsrv_devctl_writemeta);
	}
	munmap(metabuf, FLASH_PAGE_SIZE);

	return ret;
}
