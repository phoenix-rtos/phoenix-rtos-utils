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

#include "bcb.h"


int flashmng_readraw(oid_t oid, uint32_t paddr, void *data, int size);


int flashmng_writedev(oid_t oid, uint32_t paddr, void *data, int size, int type);


int flashmng_eraseBlock(oid_t oid, int start, int end, int chip_erase);


int flashmng_getAttr(int type, offs_t* val, oid_t oid);


int flashmng_checkRange(oid_t oid, int start, int end, dbbt_t **dbbt);


int flashmng_cleanMarkers(oid_t oid, int start, int end);
