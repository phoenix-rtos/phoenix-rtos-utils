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

#ifndef _FLASHMNG_H_
#define _FLASHMNG_H_

#include <sys/types.h>

#include <imx6ull-flashsrv.h>
#include <ptable.h>


extern int flashmng_isBad(const oid_t *oid, const flashsrv_info_t *info, unsigned int block);


extern int flashmng_erase(const oid_t *oid, const flashsrv_info_t *info, unsigned int start, unsigned int size);


extern int flashmng_readMeta(const oid_t *oid, const flashsrv_info_t *info, void *data, unsigned int page);


extern int flashmng_writeMeta(const oid_t *oid, const flashsrv_info_t *info, const void *data, unsigned int page);


extern int flashmng_markClean(const oid_t *oid, const flashsrv_info_t *info, unsigned int start, unsigned int size);


extern int flashmng_readPtable(const oid_t *oid, const flashsrv_info_t *info, ptable_t *ptable);


extern int flashmng_writePtable(const oid_t *oid, const flashsrv_info_t *info, ptable_t *ptable);


extern int flashmng_info(const oid_t *oid, flashsrv_info_t *info);


#endif
