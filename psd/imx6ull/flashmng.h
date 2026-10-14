/*
 * Phoenix-RTOS
 *
 * psd - Serial Download Protocol client
 *
 * Flash Server Manager.
 *
 * Copyright 2019, 2026 Phoenix Systems
 * Author: Hubert Buczynski, Ziemowit Leszczynski
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


/* Reads `size` raw bytes (data + metadata) from page offset `addr` in the partition `oid`, returns 0 or -1 */
int flashmng_readraw(oid_t oid, uint32_t addr, void *data, int size);


/* Writes data or metadata (`type`: flashsrv_devctl_write*) at offset `addr` in the partition, returns 0 or -1 */
int flashmng_writedev(oid_t oid, uint32_t addr, void *data, int size, int type);


/* Erases `size` bytes (0: whole partition) from offset `start` in the partition, skips bad blocks; returns 0 or -1 */
int flashmng_eraseBlocks(oid_t oid, unsigned int start, unsigned int size);


/* Gets attribute `type` of `oid` (atSize: partition size, atDev: its offset on the device), returns 0 or -1 */
int flashmng_getAttr(int type, long long *val, oid_t oid);


/* Adds bad blocks of the partition (`size` bytes at device offset `partOffs`) to the DBBT, returns 0 or < 0 */
int flashmng_checkRange(oid_t oid, unsigned int partOffs, unsigned int size, dbbt_t *dbbt);


/* Writes JFFS2 cleanmarkers to the good blocks of [`start`, `start` + `size`) in the partition, returns 0 or < 0 */
int flashmng_cleanMarkers(oid_t oid, unsigned int start, unsigned int size);


/* Gets the flash geometry, returns 0 or -1 */
int flashmng_getInfo(oid_t oid, flashsrv_info_t *info);


/* Checks the block at offset `addr` in the partition: 1 - bad, 0 - good, < 0 - error */
int flashmng_isBadBlock(oid_t oid, uint32_t addr);
