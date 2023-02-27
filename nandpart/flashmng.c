/*
 * Phoenix-RTOS
 *
 * NAND partition utility
 *
 * Copyright 2023 Phoenix Systems
 * Author: Lukasz Kosinski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <sys/msg.h>

#include "flashmng.h"


int flashmng_isBad(const oid_t *oid, const flashsrv_info_t *info, unsigned int block)
{
	msg_t msg = { 0 };
	flash_i_devctl_t *idevctl = (flash_i_devctl_t *)msg.i.raw;
	flash_o_devctl_t *odevctl = (flash_o_devctl_t *)msg.o.raw;
	int err;

	msg.type = mtDevCtl;
	idevctl->type = flashsrv_devctl_isbad;
	idevctl->badblock.oid = *oid;
	idevctl->badblock.address = block * info->erasesz;

	err = msgSend(oid->port, &msg);
	if (err < 0) {
		return err;
	}

	return odevctl->err;
}


int flashmng_erase(const oid_t *oid, const flashsrv_info_t *info, unsigned int start, unsigned int size)
{
	msg_t msg = { 0 };
	flash_i_devctl_t *idevctl = (flash_i_devctl_t *)msg.i.raw;
	flash_o_devctl_t *odevctl = (flash_o_devctl_t *)msg.o.raw;
	int err;

	msg.type = mtDevCtl;
	idevctl->type = flashsrv_devctl_erase;
	idevctl->erase.oid = *oid;
	idevctl->erase.address = start * info->erasesz;
	idevctl->erase.size = size * info->erasesz;

	err = msgSend(oid->port, &msg);
	if (err < 0) {
		return err;
	}

	return odevctl->err;
}


int flashmng_readMeta(const oid_t *oid, const flashsrv_info_t *info, void *data, unsigned int page)
{
	msg_t msg = { 0 };
	flash_i_devctl_t *idevctl = (flash_i_devctl_t *)msg.i.raw;
	flash_o_devctl_t *odevctl = (flash_o_devctl_t *)msg.o.raw;
	int err;

	msg.type = mtDevCtl;
	msg.o.data = data;
	msg.o.size = info->oobsz;
	idevctl->type = flashsrv_devctl_readmeta;
	idevctl->read.oid = *oid;
	idevctl->read.address = page * info->writesz;
	idevctl->read.size = info->oobsz;

	err = msgSend(oid->port, &msg);
	if (err < 0) {
		return err;
	}

	return odevctl->err;
}


int flashmng_writeMeta(const oid_t *oid, const flashsrv_info_t *info, const void *data, unsigned int page)
{
	msg_t msg = { 0 };
	flash_i_devctl_t *idevctl = (flash_i_devctl_t *)msg.i.raw;
	flash_o_devctl_t *odevctl = (flash_o_devctl_t *)msg.o.raw;
	int err;

	msg.type = mtDevCtl;
	msg.i.data = (void *)data; /* TODO: remove cast after msg.i.data becomes const void * */
	msg.i.size = info->oobsz;
	idevctl->type = flashsrv_devctl_writemeta;
	idevctl->write.oid = *oid;
	idevctl->write.address = page * info->writesz;
	idevctl->write.size = info->oobsz;

	err = msgSend(oid->port, &msg);
	if (err < 0) {
		return err;
	}

	return odevctl->err;
}


int flashmng_markClean(const oid_t *oid, const flashsrv_info_t *info, unsigned int start, unsigned int size)
{
	static const struct {
		uint16_t magic;
		uint16_t type;
		uint32_t len;
	} jffs2_cleanmarker = { 0x1985, 0x2003, 8 };
	unsigned int block, n = 0, end = start + size, npages = info->erasesz / info->writesz;
	unsigned char *buff;
	int err;

	buff = malloc(info->oobsz);
	if (buff == NULL) {
		return -ENOMEM;
	}
	memcpy(buff, &jffs2_cleanmarker, sizeof(jffs2_cleanmarker));
	memset(buff + sizeof(jffs2_cleanmarker), 0xff, info->oobsz - sizeof(jffs2_cleanmarker));

	for (block = start; block < end; block++) {
		err = flashmng_isBad(oid, info, block);
		if (err < 0) {
			break;
		}

		/* Write cleanmarker to metadata of the first page in the block */
		if (err == 0) {
			err = flashmng_writeMeta(oid, info, buff, block * npages);
			if (err < 0) {
				break;
			}
			n++;
		}
	}
	free(buff);

	return (err < 0) ? err : n;
}


int flashmng_readPtable(const oid_t *oid, const flashsrv_info_t *info, ptable_t *ptable)
{
	msg_t msg = { 0 };
	flash_i_devctl_t *idevctl = (flash_i_devctl_t *)msg.i.raw;
	flash_o_devctl_t *odevctl = (flash_o_devctl_t *)msg.o.raw;
	int err;

	msg.type = mtDevCtl;
	msg.o.data = ptable;
	msg.o.size = info->writesz;
	idevctl->type = flashsrv_devctl_readptable;
	idevctl->ptable.oid = *oid;

	err = msgSend(oid->port, &msg);
	if (err < 0) {
		return err;
	}

	return odevctl->err;
}


int flashmng_writePtable(const oid_t *oid, const flashsrv_info_t *info, ptable_t *ptable)
{
	msg_t msg = { 0 };
	flash_i_devctl_t *idevctl = (flash_i_devctl_t *)msg.i.raw;
	flash_o_devctl_t *odevctl = (flash_o_devctl_t *)msg.o.raw;
	int err;

	msg.type = mtDevCtl;
	msg.i.data = ptable;
	msg.i.size = info->writesz;
	idevctl->type = flashsrv_devctl_writeptable;
	idevctl->ptable.oid = *oid;

	err = msgSend(oid->port, &msg);
	if (err < 0) {
		return err;
	}

	return odevctl->err;
}


int flashmng_info(const oid_t *oid, flashsrv_info_t *info)
{
	msg_t msg = { 0 };
	flash_i_devctl_t *idevctl = (flash_i_devctl_t *)msg.i.raw;
	flash_o_devctl_t *odevctl = (flash_o_devctl_t *)msg.o.raw;
	int err;

	msg.type = mtDevCtl;
	idevctl->type = flashsrv_devctl_info;

	err = msgSend(oid->port, &msg);
	if (err < 0) {
		return err;
	}

	if (odevctl->err == EOK) {
		*info = odevctl->info;
	}

	return odevctl->err;
}
