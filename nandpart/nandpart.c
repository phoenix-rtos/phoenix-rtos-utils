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

#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/list.h>
#include <sys/mount.h>
#include <sys/msg.h>
#include <sys/types.h>

#include "flashmng.h"


typedef struct _nandpart_node_t {
	ptable_part_t part;
	struct _nandpart_node_t *prev, *next;
} nandpart_node_t;


typedef struct {
	nandpart_node_t *head;
	unsigned int size;
} nandpart_list_t;


static struct {
	/* Partition table device */
	flashsrv_info_t info; /* Device info */
	oid_t oid;            /* Device oid */
	int fd;               /* Device file descriptor */

	/* Partition table files */
	FILE *input;  /* Input file stream */
	FILE *output; /* Output file stream */
	FILE *update; /* Update file stream */

	/* Actions to process */
	nandpart_list_t add; /* Partitions to add */
	nandpart_list_t mod; /* Partitions to modify */
	nandpart_list_t rem; /* Partitions to remove */
} nandpart_common;


static void nandpart_help(const char *prog)
{
	printf("Usage: %s [options] <device>\n", prog);
	printf("\t-i <file>                            - input partition table file\n");
	printf("\t                                       (if not used read partition table from device)\n");
	printf("\t-o <file>                            - output partition table file\n");
	printf("\t                                       (if not used write partition table on device)\n");
	printf("\t-u <file>                            - update partition table to one defined in the file\n");
	printf("Partition table operations:\n");
	printf("\t-a <name:offs:size:type>             - add new partition\n");
	printf("\t                                       (offs and size are given in eraseblocks)\n");
	printf("\t-m <name:new_offs:new_size:new_type> - modify existing partition\n");
	printf("\t                                       (new_offs and new_size are given in eraseblocks)\n");
	printf("\t-r <name>                            - remove existing partition\n");
	printf("Supported partition types: raw, jffs2, meterfs\n");
}


static int nandpart_readPtable(FILE *file, ptable_t *ptable)
{
	uint32_t count, size;
	int err;

	/* Read partition table from device */
	if (file == NULL) {
		err = flashmng_readPtable(&nandpart_common.oid, &nandpart_common.info, ptable);
		if (err < 0) {
			if (err != -ENOENT) {
				fprintf(stderr, "nandpart: failed to read device partition table, err: %s\n", strerror(err));
			}
			return err;
		}
	}
	/* Read partition table from file */
	else {
		if (fseek(file, 0, SEEK_SET) != 0) {
			err = -errno;
			fprintf(stderr, "nandpart: failed to seek on partition table file, err: %s\n", strerror(err));
			return err;
		}

		if (fread(&count, sizeof(count), 1, file) != 1) {
			err = -EIO;
			fprintf(stderr, "nandpart: failed to read partition table file, err: %s\n", strerror(err));
			return err;
		}
		count = le32toh(count);

		size = ptable_size(count);
		if (size > nandpart_common.info.writesz) {
			err = -EINVAL;
			fprintf(stderr, "nandpart: invalid partition table file, err: %s\n", strerror(err));
			return err;
		}

		if (fseek(file, 0, SEEK_SET) != 0) {
			err = -errno;
			fprintf(stderr, "nandpart: failed to seek on partition table file, err: %s\n", strerror(err));
			return err;
		}

		if (fread(ptable, size, 1, file) != 1) {
			err = -EIO;
			fprintf(stderr, "nandpart: failed to read partition table file, err: %s\n", strerror(err));
			return err;
		}

		err = ptable_deserialize(ptable, nandpart_common.info.size, nandpart_common.info.erasesz);
		if (err < 0) {
			fprintf(stderr, "nandpart: invalid partition table file, err: %s\n", strerror(err));
			return err;
		}
	}

	return EOK;
}


static int nandpart_writePtable(FILE *file, ptable_t *ptable)
{
	uint32_t size;
	int err;

	/* Write partition table on device */
	if (file == NULL) {
		err = flashmng_writePtable(&nandpart_common.oid, &nandpart_common.info, ptable);
		if (err < 0) {
			fprintf(stderr, "nandpart: failed to write partition table on device, err: %s\n", strerror(err));
			return err;
		}
	}
	/* Write partition table to file */
	else {
		if (ftruncate(fileno(file), 0) != 0) {
			err = -errno;
			fprintf(stderr, "nandpart: failed to truncate partition table file, err: %s\n", strerror(err));
			return err;
		}

		if (fseek(file, 0, SEEK_SET) != 0) {
			err = -errno;
			fprintf(stderr, "nandpart: failed to seek on partition table file, err: %s\n", strerror(err));
			return err;
		}

		size = ptable_size(ptable->count);
		if (size > nandpart_common.info.writesz) {
			err = -EINVAL;
			fprintf(stderr, "nandpart: invalid partition table, err: %s\n", strerror(err));
			return err;
		}

		err = ptable_serialize(ptable, nandpart_common.info.size, nandpart_common.info.erasesz);
		if (err < 0) {
			fprintf(stderr, "nandpart: invalid partition table, err: %s\n", strerror(err));
			return err;
		}

		if (fwrite(ptable, size, 1, file) != 1) {
			err = -EIO;
			fprintf(stderr, "nandpart: failed to write partition table to file, err: %s\n", strerror(err));
			return err;
		}
	}

	return EOK;
}


static int nandpart_copyBlock(unsigned int srcBlock, unsigned int dstBlock, unsigned char *buff)
{
	unsigned int i, page, npages;
	int err;

	npages = nandpart_common.info.erasesz / nandpart_common.info.writesz;
	for (page = 0; page < npages; page++) {
		/* Read page metadata */
		err = flashmng_readMeta(&nandpart_common.oid, &nandpart_common.info, buff, srcBlock * npages + page);
		if (err < 0) {
			return err;
		}

		/* Check for clean page metadata */
		for (i = 0; i < nandpart_common.info.oobsz; i++) {
			if (buff[i] != 0xff) {
				break;
			}
		}

		/* Don't write clean page metadata (assume erased) */
		if (i < nandpart_common.info.oobsz) {
			err = flashmng_writeMeta(&nandpart_common.oid, &nandpart_common.info, buff, dstBlock * npages + page);
			if (err < 0) {
				return err;
			}
		}

		/* Read page data */
		if (lseek(nandpart_common.fd, (off_t)(srcBlock * npages + page) * nandpart_common.info.writesz, SEEK_SET) < 0) {
			return -errno;
		}

		if (read(nandpart_common.fd, buff, nandpart_common.info.writesz) != nandpart_common.info.writesz) {
			return -EIO;
		}

		/* Check for clean page data */
		for (i = 0; i < nandpart_common.info.writesz; i++) {
			if (buff[i] != 0xff) {
				break;
			}
		}

		/* Don't write clean page data (assume erased) */
		if (i < nandpart_common.info.writesz) {
			if (lseek(nandpart_common.fd, (off_t)(dstBlock * npages + page) * nandpart_common.info.writesz, SEEK_SET) < 0) {
				return -errno;
			}

			if (write(nandpart_common.fd, buff, nandpart_common.info.writesz) != nandpart_common.info.writesz) {
				return -EIO;
			}
		}
	}

	return EOK;
}


static int nandpart_prevBlock(unsigned int start, unsigned int *end)
{
	unsigned int block;
	int err;

	for (block = *end; start < block; block--) {
		err = flashmng_isBad(&nandpart_common.oid, &nandpart_common.info, block - 1);
		if (err <= 0) {
			if (err == 0) {
				*end = block;
			}
			return err;
		}
	}

	return 1;
}


static int nandpart_nextBlock(unsigned int *start, unsigned int end)
{
	unsigned int block;
	int err;

	for (block = *start; block < end; block++) {
		err = flashmng_isBad(&nandpart_common.oid, &nandpart_common.info, block);
		if (err <= 0) {
			if (err == 0) {
				*start = block;
			}
			return err;
		}
	}

	return 1;
}


static int nandpart_copyBackward(unsigned int srcStart, unsigned int srcEnd, unsigned int dstStart, unsigned int dstEnd, unsigned char *buff, unsigned int *src, unsigned int *dst, int failBad)
{
	int err;

	for (; (err = nandpart_prevBlock(srcStart, &srcEnd)) == 0; srcEnd--, dstEnd--) {
		for (; (err = nandpart_prevBlock(dstStart, &dstEnd)) == 0; dstEnd--) {
			if (dstEnd == srcEnd) {
				return EOK;
			}

			err = flashmng_erase(&nandpart_common.oid, &nandpart_common.info, dstEnd - 1, 1);
			if (err != 0) {
				if (err == 1) {
					err = EOK;
				}
				break;
			}

			/* New bad block */
			if (failBad == 1) {
				err = -EBADMSG;
				break;
			}
		}

		if (err != EOK) {
			if (err == 1) {
				err = -ENOSPC;
			}
			break;
		}

		err = nandpart_copyBlock(srcEnd - 1, dstEnd - 1, buff);
		if (err < 0) {
			break;
		}

		/* Update last copied source block */
		if (src != NULL) {
			*src = srcEnd - 1;
		}

		/* Update last copied destination block */
		if (dst != NULL) {
			*dst = dstEnd - 1;
		}
	}

	return err;
}


static int nandpart_copyForward(unsigned int srcStart, unsigned int srcEnd, unsigned int dstStart, unsigned int dstEnd, unsigned char *buff, unsigned int *src, unsigned int *dst, int failBad)
{
	int err;

	for (; (err = nandpart_nextBlock(&srcStart, srcEnd)) == 0; srcStart++, dstStart++) {
		for (; (err = nandpart_nextBlock(&dstStart, dstEnd)) == 0; dstStart++) {
			if (dstStart == srcStart) {
				return EOK;
			}

			err = flashmng_erase(&nandpart_common.oid, &nandpart_common.info, dstStart, 1);
			if (err != 0) {
				if (err == 1) {
					err = EOK;
				}
				break;
			}

			/* New bad block */
			if (failBad == 1) {
				err = -EBADMSG;
				break;
			}
		}

		if (err != EOK) {
			if (err == 1) {
				err = -ENOSPC;
			}
			break;
		}

		err = nandpart_copyBlock(srcStart, dstStart, buff);
		if (err < 0) {
			break;
		}

		/* Update last copied source block */
		if (src != NULL) {
			*src = srcStart;
		}

		/* Update last copied destination block */
		if (dst != NULL) {
			*dst = dstStart;
		}
	}

	return err;
}


static int nandpart_shiftRightSafe(unsigned int *start, unsigned int *end, unsigned int maxEnd, unsigned char *buff)
{
	unsigned int src, dst, dstEnd, srcStart, srcEnd;
	int err;

	srcStart = *start;
	src = dst = dstEnd = *end;

	do {
		srcEnd = dstEnd;
		err = nandpart_nextBlock(&dstEnd, maxEnd);
		if (err != 0) {
			if (err == 1) {
				err = -ENOSPC;
				break;
			}
		}
		err = nandpart_copyBackward(srcStart, srcEnd, srcStart + 1, ++dstEnd, buff, &src, &dst, 1);
	} while (err == -EBADMSG);

	/* Copy failed, try to revert it */
	if (err < 0) {
		nandpart_copyForward(dst, dstEnd, src, srcEnd, buff, NULL, NULL, 1);
	}
	/* Update start and end after shift */
	else {
		*start = dst;
		*end = dstEnd;
	}

	return err;
}


static int nandpart_eraseBlocks(unsigned int start, unsigned int size, int markClean)
{
	int err;

	err = flashmng_erase(&nandpart_common.oid, &nandpart_common.info, start, size);
	if ((err <= 0) || (markClean == 0)) {
		return err;
	}

	err = flashmng_markClean(&nandpart_common.oid, &nandpart_common.info, start, size);
	if (err < 0) {
		return err;
	}

	return EOK;
}


static int nandpart_copyBackwardSafe(unsigned int srcStart, unsigned int srcEnd, unsigned int dstStart, unsigned int dstEnd, unsigned int maxEnd, unsigned char *buff, int markClean)
{
	unsigned int src, dst, osrcEnd, odstEnd;
	int err;

	src = osrcEnd = srcEnd;
	dst = odstEnd = dstEnd;

	for (; (err = nandpart_copyBackward(srcStart, srcEnd, dstStart, dstEnd, buff, &src, &dst, 1)) == -EBADMSG;) {
		err = nandpart_shiftRightSafe(&dst, &odstEnd, maxEnd, buff);
		if (err < 0) {
			break;
		}
		srcEnd = src;
		dstEnd = dst;
	}

	/* Copy failed, try to revert it */
	if (err < 0) {
		nandpart_copyForward(dst, odstEnd, src, osrcEnd, buff, NULL, NULL, 1);
	}
	/* Erase remaining space */
	else if (maxEnd > odstEnd) {
		err = nandpart_eraseBlocks(odstEnd, maxEnd - odstEnd, markClean);
	}

	return err;
}


static int nandpart_copyForwardSafe(unsigned int srcStart, unsigned int srcEnd, unsigned int dstStart, unsigned int dstEnd, unsigned char *buff, int markClean)
{
	unsigned int src, dst;
	int err;

	err = nandpart_copyForward(srcStart, srcEnd, dstStart, dstEnd, buff, &src, &dst, 0);
	/* Copy failed, try to revert it */
	if (err < 0) {
		nandpart_copyBackward(dstStart, dst + 1, srcStart, src + 1, buff, NULL, NULL, 1);
	}
	/* Erase remaining space */
	else if (dstEnd > dst + 1) {
		err = nandpart_eraseBlocks(dst + 1, dstEnd - dst - 1, markClean);
	}

	return err;
}


static int nandpart_countBlocks(unsigned int start, unsigned int end)
{
	unsigned int n;
	int err;

	for (n = 0; (err = nandpart_nextBlock(&start, end)) == 0; n++) {
		start++;
	}

	return (err < 0) ? err : n;
}


static int nandpart_movePart(const ptable_part_t *part, const ptable_part_t *mod)
{
	unsigned int i, n, srcStart, srcEnd, dstStart, dstEnd, end;
	int err, markClean = (mod->type == ptable_jffs2) ? 1 : 0;
	uint8_t *buff;

	buff = malloc(nandpart_common.info.writesz);
	if (buff == NULL) {
		return -ENOMEM;
	}

	srcStart = part->offset / nandpart_common.info.erasesz;
	srcEnd = (part->offset + part->size) / nandpart_common.info.erasesz;

	dstStart = mod->offset / nandpart_common.info.erasesz;
	dstEnd = (mod->offset + mod->size) / nandpart_common.info.erasesz;

	/* Copy backwards */
	if ((dstStart > srcStart) && (dstStart < srcEnd)) {
		/* Count number of blocks to copy */
		err = nandpart_countBlocks(srcStart, srcEnd);
		if (err <= 0) {
			free(buff);
			return err;
		}
		n = err;

		/* Find end block of destination area */
		end = dstStart;
		for (i = 0; i < n; i++, end++) {
			err = nandpart_nextBlock(&end, dstEnd);
			if (err != 0) {
				if (err == 1) {
					err = -ENOSPC;
				}
				free(buff);
				return err;
			}
		}

		err = nandpart_copyBackwardSafe(srcStart, srcEnd, dstStart, ++end, dstEnd, buff, markClean);
	}
	/* Copy forwards */
	else {
		err = nandpart_copyForwardSafe(srcStart, srcEnd, dstStart, dstEnd, buff, markClean);
	}
	free(buff);

	return err;
}


static const char *nandpart_partType2Str(uint8_t type)
{
	switch (type) {
		case ptable_raw:
			return "raw";

		case ptable_jffs2:
			return "jffs2";

		case ptable_meterfs:
			return "meterfs";

		default:
			return "err";
	}
}


static uint8_t nandpart_partStr2Type(const char *str)
{
	if (strcmp(str, "raw") == 0) {
		return ptable_raw;
	}
	else if (strcmp(str, "jffs2") == 0) {
		return ptable_jffs2;
	}
	else if (strcmp(str, "meterfs") == 0) {
		return ptable_meterfs;
	}
	else {
		return -1;
	}
}


static const char *nandpart_parsePartName(const char *arg, ptable_part_t *part)
{
	size_t i;

	for (i = 0; i < sizeof(part->name); i++) {
		if ((arg[i] == ':') || (arg[i] == '\0')) {
			break;
		}
	}

	if ((i == 0) || (i >= sizeof(part->name))) {
		return NULL;
	}

	strncpy((char *)part->name, arg, i);
	part->name[i] = '\0';

	return arg + i;
}


static int nandpart_parsePart(const char *arg, ptable_part_t *part)
{
	char *endptr;

	arg = nandpart_parsePartName(arg, part);
	if ((arg == NULL) || (*arg != ':')) {
		return -EINVAL;
	}

	arg++;
	part->offset = strtoul(arg, &endptr, 0);
	if ((endptr == arg) || (*endptr != ':')) {
		return -EINVAL;
	}

	arg = endptr + 1;
	part->size = strtoul(arg, &endptr, 0);
	if ((endptr == arg) || (*endptr != ':')) {
		return -EINVAL;
	}

	arg = endptr + 1;
	part->type = nandpart_partStr2Type(arg);
	if (part->type == -1) {
		return -EINVAL;
	}

	return EOK;
}


static int nandpart_verifyPart(const ptable_t *ptable, const ptable_part_t *part)
{
	const ptable_part_t *p;
	uint32_t i;

	/* Check offset and size */
	if (part->size == 0) {
		return -EINVAL;
	}

	if (part->size % nandpart_common.info.erasesz != 0) {
		return -EINVAL;
	}

	if (part->offset % nandpart_common.info.erasesz != 0) {
		return -EINVAL;
	}

	if (part->offset + part->size > nandpart_common.info.size) {
		return -EINVAL;
	}

	/* Check for overflow */
	if (part->offset + part->size < part->offset) {
		return -EINVAL;
	}

	/* Check for range overlap */
	for (i = 0; i < ptable->count; i++) {
		p = ptable->parts + i;
		/* Skip check against itself */
		if (strcmp((const char *)p->name, (const char *)part->name) == 0) {
			continue;
		}

		if ((part->offset <= p->offset + p->size - 1) && (part->offset + part->size - 1 >= p->offset)) {
			return -EINVAL;
		}
	}

	return EOK;
}


static ptable_part_t *nandpart_findPart(ptable_t *ptable, const char *name)
{
	uint32_t i;

	for (i = 0; i < ptable->count; i++) {
		if (strcmp((const char *)ptable->parts[i].name, name) == 0) {
			return ptable->parts + i;
		}
	}

	return NULL;
}


static void nandpart_printPtable(const ptable_t *ptable)
{
	const ptable_part_t *part;
	uint32_t i;

	printf("Memory size: %llu, Block size: %u, Partition table size: %u\n", nandpart_common.info.size, nandpart_common.info.erasesz, ptable_size(ptable->count));
	printf("%-8s %10s %10s %10s %8s\n", "Name", "Start", "End", "Size", "Type");
	for (i = 0; i < ptable->count; i++) {
		part = ptable->parts + i;
		printf("%-8s %10u %10u %10u %8s\n", (const char *)part->name, part->offset, part->offset + part->size, part->size, nandpart_partType2Str(part->type));
	}
}


static void nandpart_freeList(nandpart_list_t *list)
{
	nandpart_node_t *node;

	while (list->size > 0) {
		node = list->head;
		LIST_REMOVE(&list->head, node);
		list->size--;
		free(node);
	}
}


static void nandpart_done(void)
{
	nandpart_freeList(&nandpart_common.rem);
	nandpart_freeList(&nandpart_common.mod);
	nandpart_freeList(&nandpart_common.add);

	if (nandpart_common.input != NULL) {
		fclose(nandpart_common.input);
	}

	if (nandpart_common.output != NULL) {
		fclose(nandpart_common.output);
	}

	if (nandpart_common.update != NULL) {
		fclose(nandpart_common.update);
	}

	if (nandpart_common.fd != 0) {
		fsync(nandpart_common.fd);
		close(nandpart_common.fd);
	}
}


static int nandpart_umountPart(const ptable_part_t *part)
{
	char devname[sizeof(part->name) + 5] = "/dev/";

	return umount(strcat(devname, (const char *)part->name));
}


static int nandpart_processActions(ptable_t *ptable)
{
	nandpart_node_t *node;
	ptable_part_t *part;
	unsigned int i;
	int err, ret = EOK;

	/* Check number of partitions to remove */
	if (nandpart_common.rem.size > ptable->count) {
		err = -EINVAL;
		fprintf(stderr, "nandpart: invalid number of partitions to remove, err: %s\n", strerror(err));
		return err;
	}

	/* Check number of partitions to modify */
	if (nandpart_common.mod.size > ptable->count - nandpart_common.rem.size) {
		err = -EINVAL;
		fprintf(stderr, "nandpart: invalid number of partitions to modify, err: %s\n", strerror(err));
		return err;
	}

	/* Check number of partitions to add */
	if (ptable_size(ptable->count - nandpart_common.rem.size + nandpart_common.add.size) > nandpart_common.info.writesz) {
		err = -EINVAL;
		fprintf(stderr, "nandpart: invalid number of partitions to add, err: %s\n", strerror(err));
		return err;
	}

	/* Remove partitions */
	while (nandpart_common.rem.size > 0) {
		node = nandpart_common.rem.head;
		LIST_REMOVE(&nandpart_common.rem.head, node);
		nandpart_common.rem.size--;

		part = nandpart_findPart(ptable, (const char *)node->part.name);
		if (part == NULL) {
			err = -EINVAL;
			fprintf(stderr, "nandpart: no existing '%s' partition to remove, err: %s\n", (const char *)node->part.name, strerror(err));
			free(node);
			return err;
		}

		memcpy(part, part + sizeof(*part), (ptable->count - (part - ptable->parts + 1)) * sizeof(*part));
		ptable->count--;

		if (nandpart_common.output == NULL) {
			err = nandpart_umountPart(part);
			if (err < 0) {
				/* TODO: handle umount errors, ignore for now */
			}

			err = nandpart_writePtable(nandpart_common.output, ptable);
			if (err < 0) {
				free(node);
				return err;
			}

			/* Removed partition on device, EAGAIN means NAND server restart required */
			ret = -EAGAIN;
		}
		free(node);
	}

	/* Modify partitions */
	while (nandpart_common.mod.size > 0) {
		/* Find next partition to modify */
		for (i = 0, node = nandpart_common.mod.head; i < nandpart_common.mod.size; i++, node = node->next) {
			err = nandpart_verifyPart(ptable, &node->part);
			if (err == EOK) {
				break;
			}
		}

		if (i >= nandpart_common.mod.size) {
			err = -EINVAL;
			fprintf(stderr, "nandpart: no valid node with partition to modify, err: %s\n", strerror(err));
			return err;
		}
		LIST_REMOVE(&nandpart_common.mod.head, node);
		nandpart_common.mod.size--;

		part = nandpart_findPart(ptable, (const char *)node->part.name);
		if (part == NULL) {
			err = -EINVAL;
			fprintf(stderr, "nandpart: no existing '%s' partition to modify, err: %s\n", (const char *)node->part.name, strerror(err));
			free(node);
			return err;
		}

		if ((part->offset != node->part.offset) || (part->size != node->part.size) || (part->type != node->part.type)) {
			if (nandpart_common.output == NULL) {
				err = nandpart_umountPart(part);
				if (err < 0) {
					/* TODO: handle umount errors, ignore for now */
				}

				err = nandpart_movePart(part, &node->part);
				if (err < 0) {
					fprintf(stderr, "nandpart: failed to move '%s' partition, err: %s\n", (const char *)node->part.name, strerror(err));
					free(node);
					return err;
				}
			}

			/* Update partition */
			part->offset = node->part.offset;
			part->size = node->part.size;
			part->type = node->part.type;

			if (nandpart_common.output == NULL) {
				err = nandpart_writePtable(nandpart_common.output, ptable);
				if (err < 0) {
					free(node);
					return err;
				}

				/* Modified partition on device, EAGAIN means NAND server restart required */
				ret = -EAGAIN;
			}
		}
		free(node);
	}

	/* Add partitions */
	while (nandpart_common.add.size > 0) {
		node = nandpart_common.add.head;
		LIST_REMOVE(&nandpart_common.add.head, node);
		nandpart_common.mod.size--;

		memcpy(ptable->parts + ptable->count, &node->part, sizeof(node->part));
		ptable->count++;

		if (nandpart_common.output == NULL) {
			err = nandpart_writePtable(nandpart_common.output, ptable);
			if (err < 0) {
				free(node);
				return err;
			}

			/* Added partition on device, EAGAIN means NAND server restart required */
			ret = -EAGAIN;
		}
		free(node);
	}

	return ret;
}


static int nandpart_updatePtable(ptable_t *ptable, ptable_t *update)
{
	const ptable_part_t *part, *mod;
	nandpart_node_t *node;
	uint32_t i;
	int err;

	/* Find partitions to remove */
	for (i = 0; i < ptable->count; i++) {
		part = ptable->parts + i;
		mod = nandpart_findPart(update, (const char *)part->name);

		if (mod == NULL) {
			node = calloc(1, sizeof(nandpart_node_t));
			if (node == NULL) {
				err = -ENOMEM;
				fprintf(stderr, "nandpart: failed to allocate node for partition to remove, err: %s\n", strerror(err));
				return err;
			}

			node->part = *part;
			LIST_ADD(&nandpart_common.rem.head, node);
			nandpart_common.rem.size++;
		}
	}

	/* Find partitions to add/modify */
	for (i = 0; i < update->count; i++) {
		mod = update->parts + i;
		part = nandpart_findPart(ptable, (const char *)mod->name);

		if (part == NULL) {
			node = calloc(1, sizeof(nandpart_node_t));
			if (node == NULL) {
				err = -ENOMEM;
				fprintf(stderr, "nandpart: failed to allocate node for partition to add, err: %s\n", strerror(err));
				return err;
			}

			node->part = *mod;
			LIST_ADD(&nandpart_common.add.head, node);
			nandpart_common.add.size++;
		}
		else {
			node = calloc(1, sizeof(nandpart_node_t));
			if (node == NULL) {
				err = -ENOMEM;
				fprintf(stderr, "nandpart: failed to allocate node for partition to modify, err: %s\n", strerror(err));
				return err;
			}

			node->part = *mod;
			LIST_ADD(&nandpart_common.mod.head, node);
			nandpart_common.mod.size++;
		}
	}

	return EOK;
}


static int nandpart_run(void)
{
	ptable_t *ptable, *update;
	int err;

	ptable = malloc(nandpart_common.info.writesz);
	if (ptable == NULL) {
		err = -ENOMEM;
		fprintf(stderr, "nandpart: failed to allocate partition table buffer, err: %s\n", strerror(err));
		return err;
	}

	err = nandpart_readPtable(nandpart_common.input, ptable);
	if (err < 0) {
		free(ptable);
		return err;
	}

	if (nandpart_common.update != NULL) {
		update = malloc(nandpart_common.info.writesz);
		if (update == NULL) {
			err = -ENOMEM;
			fprintf(stderr, "nandpart: failed to allocate update partition table buffer, err: %s\n", strerror(err));
			free(ptable);
			return err;
		}

		err = nandpart_readPtable(nandpart_common.update, update);
		if (err < 0) {
			free(update);
			free(ptable);
			return err;
		}

		err = nandpart_updatePtable(ptable, update);
		if (err < 0) {
			free(update);
			free(ptable);
			return err;
		}
		free(update);
	}

	/* Nothing to do, print/save partition table */
	if ((nandpart_common.rem.size == 0) && (nandpart_common.mod.size == 0) && (nandpart_common.add.size == 0)) {
		/* Print partition table */
		if (nandpart_common.output == NULL) {
			nandpart_printPtable(ptable);
		}
		/* Save partition table to file */
		else {
			err = nandpart_writePtable(nandpart_common.output, ptable);
		}
	}
	/* Process actions */
	else {
		err = nandpart_processActions(ptable);
		/* Save modified partition table file */
		if ((err >= 0) && (nandpart_common.output != NULL)) {
			err = nandpart_writePtable(nandpart_common.output, ptable);
		}
	}
	free(ptable);

	return err;
}


static int nandpart_init(int argc, char *argv[])
{
	const char *arg, *input = NULL, *output = NULL, *update = NULL;
	nandpart_node_t *node;
	int c, err;
	char *dev;

	nandpart_common.input = NULL;
	nandpart_common.output = NULL;
	nandpart_common.update = NULL;
	nandpart_common.add.head = NULL;
	nandpart_common.mod.head = NULL;
	nandpart_common.rem.head = NULL;

	while ((c = getopt(argc, argv, "i:o:u:a:m:r:h")) != -1) {
		switch (c) {
			case 'i':
				input = optarg;
				break;

			case 'o':
				output = optarg;
				break;

			case 'u':
				update = optarg;
				break;

			case 'a':
				node = calloc(1, sizeof(nandpart_node_t));
				if (node == NULL) {
					err = -ENOMEM;
					fprintf(stderr, "nandpart: failed to allocate node for partition to add, err: %s\n", strerror(err));
					return err;
				}

				err = nandpart_parsePart(optarg, &node->part);
				if (err < 0) {
					fprintf(stderr, "nandpart: invalid format of partition to add - %s, err: %s\n", optarg, strerror(err));
					return err;
				}

				LIST_ADD(&nandpart_common.add.head, node);
				nandpart_common.add.size++;
				break;

			case 'm':
				node = calloc(1, sizeof(nandpart_node_t));
				if (node == NULL) {
					err = -ENOMEM;
					fprintf(stderr, "nandpart: failed to allocate node for partition to modify, err: %s\n", strerror(err));
					return err;
				}

				err = nandpart_parsePart(optarg, &node->part);
				if (err < 0) {
					fprintf(stderr, "nandpart: invalid format of partition to modify - %s, err: %s\n", optarg, strerror(err));
					return err;
				}

				LIST_ADD(&nandpart_common.mod.head, node);
				nandpart_common.mod.size++;
				break;

			case 'r':
				node = calloc(1, sizeof(nandpart_node_t));
				if (node == NULL) {
					err = -ENOMEM;
					fprintf(stderr, "nandpart: failed to allocate node for partition to remove, err: %s\n", strerror(err));
					return err;
				}

				arg = nandpart_parsePartName(optarg, &node->part);
				if ((arg == NULL) || (*arg != '\0')) {
					err = -EINVAL;
					fprintf(stderr, "nandpart: invalid format of partition to remove - %s, err: %s\n", optarg, strerror(err));
					return err;
				}

				LIST_ADD(&nandpart_common.rem.head, node);
				nandpart_common.rem.size++;
				break;

			case 'h':
				nandpart_help(argv[0]);
				nandpart_done();
				exit(EXIT_SUCCESS);

			default:
				nandpart_help(argv[0]);
				return -EINVAL;
		}
	}

	if (optind >= argc) {
		fprintf(stderr, "nandpart: missing device argument\n");
		nandpart_help(argv[0]);
		return -EINVAL;
	}

	dev = realpath(argv[optind], NULL);
	if (dev == NULL) {
		err = -errno;
		fprintf(stderr, "nandpart: failed to resolve %s device path, err: %s\n", argv[optind], strerror(err));
		return err;
	}

	err = lookup(dev, NULL, &nandpart_common.oid);
	if (err < 0) {
		fprintf(stderr, "nandpart: failed to lookup %s device, err: %s\n", dev, strerror(err));
		free(dev);
		return err;
	}

	err = flashmng_info(&nandpart_common.oid, &nandpart_common.info);
	if (err < 0) {
		fprintf(stderr, "nandpart: failed to get %s device info, err: %s\n", dev, strerror(err));
		free(dev);
		return err;
	}

	err = open(dev, O_RDWR);
	if (err < 0) {
		fprintf(stderr, "nandpart: failed to open %s device, err: %s\n", dev, strerror(err));
		free(dev);
		return err;
	}
	nandpart_common.fd = err;
	free(dev);

	if (input != NULL) {
		nandpart_common.input = fopen(input, "rb");
		if (nandpart_common.input == NULL) {
			err = -errno;
			fprintf(stderr, "nandpart: failed to open %s input file, err: %s\n", input, strerror(err));
			return err;
		}
	}

	if (output != NULL) {
		/* Open in append mode (don't truncate in case output is the same as input) */
		nandpart_common.output = fopen(output, "ab");
		if (nandpart_common.output == NULL) {
			err = -errno;
			fprintf(stderr, "nandpart: failed to open %s output file, err: %s\n", output, strerror(err));
			return err;
		}
	}

	if (update != NULL) {
		nandpart_common.update = fopen(update, "rb");
		if (nandpart_common.update == NULL) {
			err = -errno;
			fprintf(stderr, "nandpart: failed to open %s update file, err: %s\n", update, strerror(err));
			return err;
		}
	}

	/* Convert partitions offset and size unit from eraseblocks to bytes */
	node = nandpart_common.mod.head;
	if (node != NULL) {
		do {
			node->part.offset *= nandpart_common.info.erasesz;
			node->part.size *= nandpart_common.info.erasesz;
			node = node->next;
		} while (node != nandpart_common.mod.head);
	}

	node = nandpart_common.add.head;
	if (node != NULL) {
		do {
			node->part.offset *= nandpart_common.info.erasesz;
			node->part.size *= nandpart_common.info.erasesz;
			node = node->next;
		} while (node != nandpart_common.add.head);
	}

	return EOK;
}


int main(int argc, char *argv[])
{
	int err;

	err = nandpart_init(argc, argv);
	if (err >= 0) {
		err = nandpart_run();
	}
	nandpart_done();

	return (err < 0) ? -err : EXIT_SUCCESS;
}
