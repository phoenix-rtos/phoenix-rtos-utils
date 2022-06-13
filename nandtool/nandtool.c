/*
 * Phoenix-RTOS
 *
 * NANDtool utility
 *
 * Allows to program/erase NAND flash memory
 *
 * Copyright 2018, 2021 Phoenix Systems
 * Author: Kamil Amanowicz, Lukasz Kosinski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/file.h>
#include <sys/msg.h>
#include <sys/stat.h>

#include "flashmng.h"


static struct {
	oid_t oid;
	int fd;
	flashsrv_info_t *info;
	int interactive;
} nandtool_common;


static int nandtool_flash(const char *path, unsigned int start, int raw)
{
	const flashsrv_info_t *info = nandtool_common.info;
	const unsigned int npages = info->erasesz / info->writesz;
	const unsigned int pagesz = raw ? info->writesz + info->metasz : info->writesz;
	unsigned int page, block, boffs, offs = 0;
	unsigned int perc, oldperc = 0;
	struct stat stat;
	char *buff;
	int fd, err;

	if ((err = open(path, O_RDONLY)) < 0) {
		fprintf(stderr, "nandtool: failed to open file %s, err: %d\n", path, err);
		return err;
	}
	fd = err;

	if ((buff = malloc(pagesz)) == NULL) {
		err = -ENOMEM;
		fprintf(stderr, "nandtool: failed to allocate buffer, err: %d\n", err);
		close(fd);
		return err;
	}

	do {
		if ((err = fstat(fd, &stat)) < 0) {
			fprintf(stderr, "nandtool: failed to stat file %s, err: %d\n", path, err);
			break;
		}

		start *= npages;
		while (offs < stat.st_size) {
			page = start + (offs / pagesz);
			block = page / npages;

			if ((err = flashmng_isbad(nandtool_common.oid, block)) < 0) {
				fprintf(stderr, "nandtool: failed to check block %u, err: %d\n", block, err);
				break;
			}
			else if (err > 0) {
				start++;
				continue;
			}

			memset(buff, 0, pagesz);
			for (boffs = 0; boffs < pagesz; boffs += err) {
				if ((err = read(fd, buff + boffs, pagesz - boffs)) <= 0) {
					if (err < 0) {
						err = -errno;
						fprintf(stderr, "nandtool: failed to read file %s, err: %d\n", path, err);
					}
					break;
				}
			}

			if (err < 0)
				break;

			if (raw) {
				if ((err = flashmng_writeraw(nandtool_common.oid, page, buff, pagesz)) < 0) {
					fprintf(stderr, "nandtool: failed to write raw data to page %u, err: %d\n", page, err);
					break;
				}
			}
			else {
				if (lseek(nandtool_common.fd, (off_t)page * pagesz, SEEK_SET) < 0) {
					err = -errno;
					fprintf(stderr, "nandtool: failed to lseek to page %u, err: %d\n", page, err);
					break;
				}

				if ((err = write(nandtool_common.fd, buff, pagesz)) != pagesz) {
					err = -errno;
					fprintf(stderr, "nandtool: failed to write data to page %u, err: %d\n", page, err);
					break;
				}
			}

			offs += boffs;

			perc = (100 * offs) / stat.st_size;
			if (nandtool_common.interactive) {
				printf("\rFlashing %s %2u%%...", path, perc);
			}
			else if (perc - oldperc >= 10) {
				printf("Flashing %s %2u%%\n", path, perc);
				fflush(stdout);
				oldperc = perc;
			}
		}

		if (err < 0)
			break;

		printf("\rFlashing %s completed!\n", path);
		err = 0;
	} while (0);

	free(buff);
	close(fd);

	return err;
}


static int nandtool_erase(unsigned int start, unsigned int size, int write_cleanmarkers)
{
	int err;

	if ((err = flashmng_erase(nandtool_common.oid, start, size)) < 0) {
		fprintf(stderr, "nandtool: failed to erase blocks, err: %d\n", err);
		return err;
	}

	if (write_cleanmarkers) {
		/* we're iterating over erase blocks on our side, so we need to know the real partition size */
		if (size == 0)
			size = nandtool_common.info->size / nandtool_common.info->erasesz;

		if ((err = flashmng_cleanMarkers(nandtool_common.oid, start, size)) < 0) {
			fprintf(stderr, "nandtool: failed to write cleanmarkers, err: %d\n", err);
			return err;
		}
	}

	return EOK;
}


static int nandtool_check(void)
{
	int err;

	if ((err = flashmng_checkbad(nandtool_common.oid)) < 0) {
		fprintf(stderr, "nandtool: failed to complete bad block check, err: %d\n", err);
		return err;
	}

	return EOK;
}


static void nandtool_help(const char *prog)
{
	printf("Usage: %s [options] <device>\n", prog);
	printf("\t-e <start[:size]> - erase size block(s) starting from start\n");
	printf("\t                    (skip size to erase one block or pass size=0 to erase whole device)\n");
	printf("\t-j                - write jffs2 cleanmarkers (valid only with erase operation)\n");
	printf("\t-c                - check device for bad blocks and print summary\n");
	printf("\n");
	printf("\t-i <path>         - path of the file to flash (requires -s option)\n");
	printf("\t-r                - flash raw data\n");
	printf("\t-s <block>        - start flashing from given block (requires -i)\n");
	printf("\t-h                - print this help message\n");
	printf("\t-q                - quiet: don't print flashing progress\n");
}


int main(int argc, char **argv)
{
	int check = 0, raw = 0, flash_start = -1, erase_start = -1, erase_size = -1, write_cleanmarkers = 0;
	char *dev, *tok, *path = NULL;
	int c, err;

	if (isatty(STDOUT_FILENO))
		nandtool_common.interactive = 1;

	while ((c = getopt(argc, argv, "e:i:r:s:chjq")) != -1) {
		switch (c) {
			case 'e':
				tok = strtok(optarg, ":");
				erase_start = strtoul(tok, NULL, 0);
				erase_size = ((tok = strtok(NULL, ":")) == NULL) ? 1 : strtoul(tok, NULL, 0);
				break;

			case 'j':
				write_cleanmarkers = 1;
				break;

			case 'i':
				path = optarg;
				raw = 0;
				break;

			case 'r':
				raw = 1;
				break;

			case 's':
				flash_start = strtoul(optarg, NULL, 0);
				break;

			case 'c':
				check = 1;
				break;

			case 'q':
				nandtool_common.interactive = 0;
				break;

			case 'h':
			default:
				nandtool_help(argv[0]);
				return 0;
		}
	}

	if (optind >= argc) {
		fprintf(stderr, "nandtool: missing device arg\n");
		nandtool_help(argv[0]);
		return 1;
	}

	if ((dev = realpath(argv[optind], NULL)) == NULL) {
		fprintf(stderr, "nandtool: failed to resolve path (%s): %s\n", argv[optind], strerror(errno));
		return 1;
	}


	if ((err = lookup(dev, NULL, &nandtool_common.oid)) < 0) {
		fprintf(stderr, "nandtool: failed to lookup device (%s), err: %d\n", dev, err);
		free(dev);
		return 1;
	}

	if ((err = open(dev, O_RDWR)) < 0) {
		fprintf(stderr, "nandtool: failed to open device (%s), err: %d\n", dev, err);
		free(dev);
		return 1;
	}
	nandtool_common.fd = err;

	if ((nandtool_common.info = flashmng_info(nandtool_common.oid)) == NULL) {
		err = -EFAULT;
		fprintf(stderr, "nandtool: failed to get device info (%s), err: %d\n", dev, err);
		close(nandtool_common.fd);
		free(dev);
		return 1;
	}

	do {
		if (check && ((err = nandtool_check()) < 0))
			break;

		if ((erase_start >= 0) && (erase_size >= 0) && ((err = nandtool_erase(erase_start, erase_size, write_cleanmarkers)) < 0))
			break;

		if ((flash_start >= 0) && (path != NULL) && ((err = nandtool_flash(path, flash_start, raw)) < 0))
			break;
	} while (0);

	close(nandtool_common.fd);

	free(dev);
	return (err < 0) ? 1 : 0;
}
