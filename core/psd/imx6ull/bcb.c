/*
 * Phoenix-RTOS
 *
 * IMX6ULL NAND tool.
 *
 * Boot control blocks
 *
 * Copyright 2018 Phoenix Systems
 * Author: Kamil Amanowicz
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/mman.h>

#include "flashmng.h"
#include "bcb.h"
#include "bch.h"

#include <imx6ull-flashsrv.h>

uint32_t bcb_checksum(uint8_t *bcb, int size)
{
	int i;
	uint32_t checksum = 0;

	for (i = 0; i <= size; i++)
		checksum += bcb[i];

	checksum ^= 0xffffffff;
	return checksum;
}


void dbbt_fingerprint(dbbt_t *dbbt)
{
	dbbt->fingerprint = 0x54424244;//0x44424254;//
	dbbt->version = 0x01000000;
}


int dbbt_block_is_bad(dbbt_t *dbbt, uint32_t block_num)
{
	int i;

	if (dbbt == NULL)
		return 0;

	for (i = 0; i < dbbt->entries_num; i++) {
		if (block_num == dbbt->bad_block[i])
			return 1;
	}

	return 0;
}


/* FIXME: current DBBT implementation isn't recognized as a valid one by the BootROM */
/* The following DBBT test should pass (it currently fails): */
/* 1. Enable DBBT usage in FCB by setting dbbt_start = 0x100 and disable_bbm_search = 1 */
/* 2. Add FW1 block (8th block) to BBT in flashmng_checkRange() */
/* 3. Flash FW1 onto the next block (9th block) */
/* 4. BootROM should detect FW1 block as a badblock and load firmware from the next block */
int dbbt_flash(oid_t oid, int fd, dbbt_t *dbbt, const flashsrv_info_t *info)
{
	unsigned int i, dbbt_failed = 0;
	int err;

	/* DBBT is just after BCB_CNT FCB blocks on the same partition */
	offs_t partoff = BCB_CNT * info->erasesz;

	dbbt_fingerprint(dbbt);
	dbbt->size = 1;

	for (i = 0; i < BCB_CNT; partoff += info->erasesz, i++) {
		if ((err = lseek(fd, partoff, SEEK_SET)) < 0) {
			printf("DBBT%u FAIL: lseek(1) to 0x%llx error, %d\n ", i, partoff, err);
			dbbt_failed++;
			continue;
		}

		if (write(fd, dbbt, info->writesz) != info->writesz) {
			printf("DBBT%u FAIL: error writing dbbt page\n", i);
			dbbt_failed++;
			continue;
		}

		if (dbbt->entries_num) {
			if ((err = lseek(fd, partoff + (4 * info->writesz), SEEK_SET)) < 0) {
				printf("DBBT%u FAIL: lseek(2) to 0x%llx error, %d\n", i, partoff + (4 * info->writesz), err);
				dbbt_failed++;
				continue;
			}

			if (write(fd, (char *)dbbt + 4 * info->writesz, info->writesz) != info->writesz) {
				printf("DBBT%u FAIL: error writing with offset dbbt page\n", i);
				dbbt_failed++;
				continue;
			}
		}
	}

	if (dbbt_failed > 0)
		printf("WARN: %u out of %u DBBT are broken\n", dbbt_failed, BCB_CNT);

	return (dbbt_failed == BCB_CNT);
}


void fcb_init(fcb_t *fcb, const flashsrv_info_t *info)
{
	fcb->fingerprint = 0x20424346;
	fcb->version = 0x01000000;
	fcb->data_setup = 0x78;
	fcb->data_hold = 0x3c;
	fcb->address_setup = 0x19; /* FIXME: the same configuration as in GPMI? */
	fcb->dsample_time = 0x6;
	fcb->nand_timing_state = 0x0;
	fcb->REA = 0x0;
	fcb->RLOH = 0x0;
	fcb->RHOH = 0x0;
	fcb->page_size = info->writesz;
	fcb->total_page_size = info->writesz + info->metasz;
	fcb->block_size = info->erasesz / info->writesz;
	fcb->b0_ecc_type = 0x8; /* FIXME */
	fcb->b0_ecc_size = 0x0; /* FIXME */
	fcb->bn_ecc_size = 512;
	fcb->bn_ecc_type = 0x7;
	fcb->meta_size = 0x10;
	fcb->ecc_per_page = 8;
	fcb->fw1_start = 8 * 64;
	fcb->fw2_start = 24 * 64;
	fcb->fw1_size = 0x1;
	fcb->fw2_size = 0x1;
	fcb->dbbt_start = 0x0; /* don't load DBBT, use badblock markers only */
	fcb->bbm_offset = 0x1000;
	fcb->bbm_start = 0x0;
	fcb->bbm_phys_offset = 0x1000;
	fcb->bch_type = 0x0;
	fcb->read_latency = 0x0;
	fcb->preamble_delay = 0x0;
	fcb->ce_delay = 0x0;
	fcb->postamble_delay = 0x0;
	fcb->cmd_add_pause = 0x0;
	fcb->data_pause = 0x0;
	fcb->speed = 0x0;
	fcb->busy_timeout = 0xffff;
	fcb->bbm_disabled = 1; /* disable badblock marker swapping */
	fcb->bbm_spare_offset = 0;
	fcb->disable_bbm_search = 0; /* use badblock markers when loading firmware */

	fcb->checksum = bcb_checksum(((uint8_t *)fcb) + 4, sizeof(fcb_t) - 4);
}


int fcb_flash(oid_t oid, const flashsrv_info_t *info)
{
	char *sbuf, *tbuf;
	fcb_t *fcb;
	unsigned int i, j, fcb_failed = 0;
	int err = 0;
	const unsigned int pages_per_block = info->erasesz / info->writesz;
	const unsigned int raw_page_size = info->writesz + info->metasz;

	sbuf = calloc(4, info->writesz);
	if (!sbuf)
		return -1;

	tbuf = sbuf + 2 * info->writesz;

	fcb = (fcb_t *)(sbuf);
	fcb_init(fcb, info);

	encode_bch_ecc(sbuf, sizeof(fcb_t), tbuf, raw_page_size, 3);

	/* put 0xff as first metadata byte (6ULL BCB has 32 bytes of metadata) to avoid badblock false positive */
	tbuf[0] = 0xff;

	for (i = 0; i < BCB_CNT; i++) {
		/* TODO: check for badblock? */
		if ((err = flashmng_writedev(oid, FCB_START + (i * pages_per_block * raw_page_size), tbuf, raw_page_size, flashsrv_devctl_writeraw)) < 0) {
			printf("FCB%u FAIL: writeraw() error, %d\n", i, err);
			fcb_failed++;
			continue;
		}

		/* verify by reading again */
		memset(sbuf, 0, 2 * info->writesz);
		if ((err = flashmng_readraw(oid, FCB_START + (i * pages_per_block * raw_page_size), sbuf, raw_page_size)) < 0) {
			printf("FCB%u FAIL: readraw() error, %d\n", i, err);
			fcb_failed++;
			continue;
		}

		for (j = 0; j < raw_page_size; ++j) {
			if (sbuf[j] != tbuf[j]) {
				printf("FCB%u FAIL[%4u]: W:0x%02x != R:0x%02x\n", i, j, tbuf[j], sbuf[j]);
				fcb_failed++;
				break;
			}
		}
	}

	free(sbuf);

	if (fcb_failed > 0)
		printf("WARN: %u out of %u FCB are broken\n", fcb_failed, BCB_CNT);

	return (fcb_failed == BCB_CNT);
}
