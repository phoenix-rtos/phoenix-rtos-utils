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

#include <sys/types.h>

#include <imx6ull-flashsrv.h>


int flashmng_readraw(oid_t oid, offs_t addr, void *data, size_t size);


int flashmng_writeraw(oid_t oid, unsigned int page, const void *data, size_t size);


int flashmng_erase(oid_t oid, unsigned int start, unsigned int end);


int flashmng_checkbad(oid_t oid);


int flashmng_isbad(oid_t oid, unsigned int block);


/* write JFFS2 clean block markers */
int flashmng_cleanMarkers(oid_t oid, unsigned int start, unsigned int size);


flashsrv_info_t *flashmng_info(oid_t oid);
