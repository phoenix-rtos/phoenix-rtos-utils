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

#include <sys/types.h>
#include <sys/msg.h>
#include <stdint.h>

#include <imx6ull-flashsrv.h>
#include "bcb.h"


int flashmng_readraw(oid_t oid, uint32_t addr, void *data, int size);


int flashmng_writedev(oid_t oid, uint32_t addr, void *data, int size, int type);


int flashmng_eraseBlocks(oid_t oid, unsigned int start, unsigned int size);


int flashmng_getAttr(int type, long long *val, oid_t oid);


int flashmng_checkRange(oid_t oid, unsigned int start, unsigned int size, dbbt_t **dbbt);


int flashmng_cleanMarkers(oid_t oid, unsigned int start, unsigned int size);


int flashmng_getInfo(oid_t oid, flashsrv_info_t *info);


int flashmng_isBadBlock(oid_t oid, uint32_t addr);
