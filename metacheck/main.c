/*
 * Phoenix-RTOS
 *
 * Meta Check
 *
 * Metadata checker for imx6ull NAND flash
 *
 * Copyright 2022 Phoenix Systems
 * Author: Maciej Purski
 *
 */

/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License along
* with this program; if not, write to the Free Software Foundation, Inc.,
* 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <imx6ull-flashsrv.h>

#include <sys/mman.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/msg.h>
#include <sys/stat.h>

#include "bch.h"

#define PAGE_SZ        (4096 + 256)
#define META_SZ        16
#define METAECC_SZ     26
#define CLEANMARKER_SZ (META_SZ + METAECC_SZ)

#define METAECC_STRENGTH 16
#define METAECC_GF       13


typedef struct partition {
	oid_t oid;
	uint32_t rawpagesz;
	uint32_t nblocks;
	uint32_t rawblocksz;
} partition_t;


static struct {
	uint8_t buf[PAGE_SZ];
	partition_t *partitions;
	int nparts;
	int pageMode;
	int verbose;
} common;


static int readraw(oid_t oid, uint32_t addr, void *data, int size)
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

	if (msgSend(oid.port, &msg) < 0) {
		return -1;
	}

	if (odevctl->err != size) {
		return odevctl->err;
	}

	return 0;
}


static int containsCleanmarker(const uint8_t *buf)
{
	static const uint8_t cleanmarker[] = { 133, 25, 3, 32, 8, 0, 0, 0, 255, 255, 255, 255, 255, 255, 255, 255 };

	return (memcmp(buf, cleanmarker, sizeof(cleanmarker)) == 0) ? 1 : 0;
}


static int isBadBlock(const uint8_t *buf)
{
	if (buf[0] == 0 && buf[1] == 0) {
		return 1;
	}

	return 0;
}


static int isErased(const uint8_t *buf, size_t buflen)
{
	int i;

	for (i = 0; i < META_SZ; i++) {
		if (buf[i] != 0xff) {
			return 0;
		}
	}

	return 1;
}


static uint8_t reverse_bit(uint8_t in_byte)
{
	uint8_t out_byte = in_byte;

	out_byte = (((out_byte & 0xaa) >> 1) | ((out_byte & 0x55) << 1));
	out_byte = (((out_byte & 0xcc) >> 2) | ((out_byte & 0x33) << 2));
	out_byte = (((out_byte & 0xf0) >> 4) | ((out_byte & 0x0f) << 4));

	return out_byte;
}


static void encodeMeta(struct bch_control *bch, const uint8_t *inbuf, uint8_t *outbuf)
{
	int i;

	memset(outbuf, 0, METAECC_SZ);
	encode_bch(bch, inbuf, META_SZ, outbuf);

	for (i = 0; i < METAECC_SZ; i++) {
		outbuf[i] = reverse_bit(outbuf[i]);
	}
}


static void dumpECC(partition_t *part, const char *type, const uint8_t *inbuf, size_t offset)
{
	size_t block = offset / part->rawblocksz;
	size_t page = (offset % part->rawblocksz) / part->rawpagesz;
	int i;

	if (!common.verbose) {
		return;
	}

	printf("Bad meta ECC (marker: %6s, block: %3u, page: %2u): ", type, block, page);
	inbuf += META_SZ;
	for (i = 0; i < METAECC_SZ; i++) {
		printf("%02x ", inbuf[i]);
	}
	putchar('\n');
}


static void dumpWeirdMeta(partition_t *part, size_t offset, const uint8_t *inbuf)
{
	size_t block = offset / part->rawblocksz;
	size_t page = (offset % part->rawblocksz) / part->rawpagesz;
	int i;

	printf("Weird metadata (block: %u: page: %u): ", block, page);

	for (i = 0; i < META_SZ; i++) {
		printf("%02x ", inbuf[i]);
	}
	putchar('\n');
}


static int checkPartition(partition_t *part, struct bch_control *bch)
{
	uint8_t eccbuf[METAECC_SZ];
	int totalcnt = 0, goodcnt = 0, badecc = 0, badcnt = 0;
	int erasedcnt = 0, weirdcnt = 0;
	int ret, eccInvalid;
	size_t offset = 0;

	ret = readraw(part->oid, 0, common.buf, part->rawpagesz);
	while (ret == 0) {
		encodeMeta(bch, common.buf, eccbuf);
		eccInvalid = memcmp(eccbuf, common.buf + META_SZ, METAECC_SZ);

		if (containsCleanmarker(common.buf)) {
			if (eccInvalid) {
				badecc++;
				dumpECC(part, "clean", common.buf, offset);
			}
			else {
				goodcnt++;
			}
		}
		else if (isErased(common.buf, META_SZ)) {
			erasedcnt++;
			/* If a page is erased we expect ECC bits to be all 0xff too */
			if (!isErased(common.buf + META_SZ, METAECC_SZ)) {
				badecc++;
				dumpECC(part, "erased", common.buf, offset);
			}
		}
		else if (isBadBlock(common.buf)) {
			badcnt++;
			if (eccInvalid) {
				badecc++;
				dumpECC(part, "bad", common.buf, offset);
			}
		}
		else {
			/* Weird, unclassified case, always dump metadata */
			weirdcnt++;

			dumpWeirdMeta(part, offset, common.buf);
			dumpECC(part, "weird", common.buf, offset);
		}

		if (common.pageMode) {
			offset += part->rawpagesz;
		}
		else {
			offset += part->rawblocksz;
		}

		totalcnt++;
		ret = readraw(part->oid, offset, common.buf, part->rawpagesz);
	}

	printf("Clean markers valid ECC: %d\n"
		"Invalid ECC:             %d\n"
		"Bad block markers:       %d\n"
		"Erased markers:          %d\n"
		"Weird metadata:          %d\n"
		"Total:                   %d\n"
		"==================================\n\n",
		goodcnt, badecc, badcnt, erasedcnt, weirdcnt, totalcnt);

	return (badecc == 0) ? 0 : 1;
}


static int partition_init(partition_t *part, const char *path)
{
	msg_t msg = { 0 };
	flash_i_devctl_t *idevctl = (flash_i_devctl_t *)msg.i.raw;
	flash_o_devctl_t *odevctl = (flash_o_devctl_t *)msg.o.raw;
	char *rpath;

	rpath = realpath(path, NULL);
	if (rpath == NULL) {
		printf("Fail to resolve path\n");
		return -1;
	}

	if (lookup(rpath, NULL, &part->oid) < 0) {
		printf("Fail to find device: %s\n", rpath);
		free(rpath);
		return -1;
	}

	msg.type = mtDevCtl;
	idevctl->type = flashsrv_devctl_info;

	if (msgSend(part->oid.port, &msg) < 0) {
		free(rpath);
		return -1;
	}

	part->rawpagesz = odevctl->info.metasz + odevctl->info.writesz;
	part->rawblocksz = (odevctl->info.erasesz / odevctl->info.writesz) * part->rawpagesz;
	free(rpath);

	return 0;
}


static void printHelp(void)
{
	printf("Usage: metacheck [OPTIONS] partition1 [partition2] ...\n"
		"Returns 0 if all partitions are correct, otherwise the return value is a bitmask,\n"
		"where i-th bit meaning that the i-th partition can't be accessed or contains bad ECC bits.\n"
		"  -p:  checks metadata of all pages, by default metacheck checks only the first page\n"
		"  -v:  verbose, dump bad ECC bytes\n"
		"  -h:  prints help\n");
}


int main(int argc, char **argv)
{
	int ret = 0, tmpret;
	int c;
	int i;
	struct bch_control *bch;

	optind = 1;
	while ((c = getopt(argc, argv, "vph")) != -1) {
		switch (c) {
			case 'v':
				common.verbose = 1;
				break;
			case 'p':
				common.pageMode = 1;
				break;
			case 'h':
			default:
				printHelp();
				return 0;
		}
	}

	/* No partitions provided */
	if (optind >= argc) {
		printHelp();
		return 1;
	}

	bch = init_bch(METAECC_GF, METAECC_STRENGTH, 0);
	if (bch == NULL) {
		printf("Fail to initialize BCH encoder\n");
		return 1;
	}

	common.nparts = argc - optind;
	common.partitions = malloc(sizeof(partition_t) * common.nparts);
	if (common.partitions == NULL) {
		printf("Out of memory!\n");
		free_bch(bch);
		return 1;
	}

	for (i = 0; i < common.nparts; i++) {
		if (partition_init(&common.partitions[i], argv[optind + i]) != 0) {
			printf("Fail to open partition: %s\n", argv[optind + i]);

			/* Return a bitmask of statuses from all the partitions */
			ret |= (1 << i);
		}
	}

	for (i = 0; i < common.nparts; i++) {
		/* Partition not initialized */
		if (ret & (1 << i)) {
			continue;
		}

		printf("Scanning partition: %s\n", argv[optind + i]);
		printf("==================================\n");
		tmpret = checkPartition(&common.partitions[i], bch);

		/* Return a bitmask of statuses from all the partitions */
		ret |= (tmpret << i);
	}

	free(common.partitions);
	free_bch(bch);

	return ret;
}
