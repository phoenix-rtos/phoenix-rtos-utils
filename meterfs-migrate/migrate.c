/*
 * Phoenix-RTOS
 *
 * meterfs migration tool
 *
 * Copyright 2022 Phoenix Systems
 * Author: Aleksander Kaminski
 *
 * %LICENSE%
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FILES         64
#define SIZE_SECTOR       4096
#define HGRAIN            32 /* Must be able to divide sector size */
#define HEADER_SECTOR_CNT 2

#define RECORD_OLD_OFFS(sector, recordsz, idx) ((sector * SIZE_SECTOR) + (idx * (recordsz + sizeof(entry_old_t))))
#define RECORD_OFFS(sector, recordsz, idx)     ((sector * SIZE_SECTOR) + (idx * (recordsz + sizeof(entry_old_t))))


typedef struct {
	unsigned int nvalid:1;
	unsigned int no:31;
} __attribute__((packed)) index_t;


typedef struct {
	unsigned int sector;
	uint32_t sectorcnt;
	uint32_t filesz;
	uint32_t recordsz;
	char name[8];
} __attribute__((packed)) fileheader_t;


typedef struct {
	index_t id;
	uint32_t filecnt;
	uint32_t checksum;
	unsigned char magic[4];
} __attribute__((packed)) header_t;


typedef struct {
	index_t id;
	uint32_t checksum;
	unsigned char data[];
} __attribute__((packed)) entry_t;


typedef struct {
	index_t id;
	uint32_t filecnt;
	unsigned char magic[4];
} __attribute__((packed)) header_old_t;


typedef struct {
	index_t id;
	unsigned char data[];
} __attribute__((packed)) entry_old_t;


static const unsigned char magicConst[4] = { 0x66, 0x41, 0x4b, 0xbb };
static const unsigned char oldMagicConst[4] = { 0xaa, 0x41, 0x4b, 0x55 };


static struct {
	unsigned char buff[SIZE_SECTOR];
	unsigned int freeSector;
	long partOffset;
} common;


/* TODO remove after fixing flash-imxrt */
static size_t fwrite_workaround(const void *ptr, size_t size, size_t nmemb, FILE *stream)
{
	static unsigned char chunkbuff[SIZE_SECTOR];
	long pos, tmp, missalign;
	size_t chunk, offset = 0;
	size_t tsize;

	tsize = size * nmemb;

	pos = ftell(stream);
	if (pos < 0) {
		return 0;
	}

	missalign = pos % SIZE_SECTOR;

	if (missalign != 0) {
		tmp = pos - missalign;
		if (fseek(stream, tmp, SEEK_SET) != 0) {
			return 0;
		}

		if (fread(chunkbuff, sizeof(chunkbuff), 1, stream) == 0) {
			return 0;
		}

		chunk = SIZE_SECTOR - missalign;
		if (chunk > tsize) {
			chunk = tsize;
		}

		memcpy(chunkbuff + missalign, ptr, chunk);

		if (fseek(stream, tmp, SEEK_SET) != 0) {
			return 0;
		}

		if (fwrite(chunkbuff, sizeof(chunkbuff), 1, stream) == 0) {
			return 0;
		}

		tsize -= chunk;
		offset += chunk;
	}

	while (tsize >= SIZE_SECTOR) {
		if (fwrite((const char *)ptr + offset, SIZE_SECTOR, 1, stream) == 0) {
			return 0;
		}

		offset += SIZE_SECTOR;
		tsize -= SIZE_SECTOR;
	}

	if (tsize != 0) {
		tmp = ftell(stream);
		if (tmp < 0) {
			return 0;
		}

		if (fread(chunkbuff, sizeof(chunkbuff), 1, stream) == 0) {
			return 0;
		}

		memcpy(chunkbuff, (const char *)ptr + offset, tsize);

		if (fseek(stream, tmp, SEEK_SET) != 0) {
			return 0;
		}

		if (fwrite(chunkbuff, sizeof(chunkbuff), 1, stream) == 0) {
			return 0;
		}
	}

	/* Mimic real fwrite - set stream position correctly. Ignore error, write has happened */
	(void)fseek(stream, pos + (size * nmemb), SEEK_SET);

	return nmemb;
}


static uint32_t calcChecksum(const void *buff, size_t bufflen)
{
	uint8_t checksum = 0;
	size_t i;

	for (i = 0; i < bufflen; ++i) {
		checksum ^= ((const uint8_t *)buff)[i];
	}

	return checksum;
}


static int getOldHeader(FILE *part, header_old_t *h)
{
	header_old_t temp;
	int ret = 0;

	if (fseek(part, common.partOffset, SEEK_SET) != 0) {
		return -1;
	}

	if (fread(h, sizeof(*h), 1, part) == 0) {
		return -1;
	}

	if (fseek(part, common.partOffset + HEADER_SECTOR_CNT * SIZE_SECTOR, SEEK_SET) != 0) {
		return -1;
	}

	if (fread(&temp, sizeof(temp), 1, part) == 0) {
		return -1;
	}

	if (h->id.nvalid != 0 || ((temp.id.nvalid == 0) && (h->id.no < temp.id.no))) {
		memcpy(h, &temp, sizeof(*h));
		ret = 1;
	}

	if (h->id.nvalid != 0) {
		return -1;
	}

	return ret;
}


static int getOldFiles(FILE *part, int whichHeader, uint32_t count, fileheader_t f[])
{
	uint32_t i;

	for (i = 0; i < count; ++i) {
		if (fseek(part, common.partOffset + (unsigned int)whichHeader * HEADER_SECTOR_CNT * SIZE_SECTOR + ((i + 1) * HGRAIN), SEEK_SET) != 0) {
			return -1;
		}

		if (fread(&f[i], sizeof(*f), 1, part) == 0) {
			return -1;
		}
	}

	return 0;
}


static uint32_t minSectorCnt(fileheader_t *f)
{
	uint32_t nrecords = f->filesz / f->recordsz;
	uint32_t bytes = nrecords * (f->recordsz + sizeof(entry_t));

	return ((bytes + (SIZE_SECTOR - 1)) / SIZE_SECTOR) + 1;
}


static int copySector(FILE *part, uint32_t dest, uint32_t src)
{
	if (fseek(part, common.partOffset + (src * SIZE_SECTOR), SEEK_SET) != 0) {
		return -1;
	}

	if (fread(common.buff, SIZE_SECTOR, 1, part) == 0) {
		return -1;
	}

	if (fseek(part, common.partOffset + (dest * SIZE_SECTOR), SEEK_SET) != 0) {
		return -1;
	}

	if (fwrite_workaround(common.buff, SIZE_SECTOR, 1, part) == 0) {
		return -1;
	}

	return 0;
}


static int eraseSector(FILE *part, uint32_t sector)
{
	memset(common.buff, 0xff, SIZE_SECTOR);

	if (fseek(part, common.partOffset + (sector * SIZE_SECTOR), SEEK_SET) != 0) {
		return -1;
	}

	if (fwrite_workaround(common.buff, SIZE_SECTOR, 1, part) == 0) {
		return -1;
	}

	return 0;
}


static int moveSectors(FILE *part, size_t from, size_t nsectors, size_t diff)
{
	int i;

	for (i = nsectors; i >= 0; --i) {
		if (copySector(part, from + i + diff, from + i) < 0) {
			return -1;
		}
	}

	return 0;
}


static int realocateFile(FILE *part, int id, int total, fileheader_t f[])
{
	uint32_t sectorsNeeded = minSectorCnt(&f[id]);
	uint32_t diff;
	int i;

	if (sectorsNeeded <= f[id].sectorcnt) {
		/* nothing to do */
		return 0;
	}

	diff = sectorsNeeded - f[id].sectorcnt;

	/* Move all files above */
	for (i = total - 1; i > id; --i) {
		if (moveSectors(part, f[i].sector, f[i].sectorcnt, diff) < 0) {
			return -1;
		}

		f[i].sector += diff;
	}

	for (i = 0; i < diff; ++i) {
		if (eraseSector(part, f[id].sector + f[id].sectorcnt - 1 + diff) < 0) {
			return -1;
		}
	}

	f[id].sectorcnt += diff;

	return 0;
}


static int updateHeader(FILE *part, header_old_t *oldheader, fileheader_t *f)
{
	header_t newheader;
	uint32_t i, j, checksum = 0;
	long headerOffs;

	for (i = 0; i < oldheader->filecnt; ++i) {
		checksum ^= calcChecksum(&f[i], sizeof(*f));
	}

	newheader.id.no = oldheader->id.no;
	newheader.id.nvalid = 0;
	memcpy(newheader.magic, magicConst, sizeof(newheader.magic));
	newheader.filecnt = oldheader->filecnt;
	newheader.checksum = 0;

	checksum ^= calcChecksum(&newheader, sizeof(newheader));
	newheader.checksum = checksum;

	for (i = 0; i < 2; ++i) {
		headerOffs = common.partOffset + i * HEADER_SECTOR_CNT * SIZE_SECTOR;

		if (fseek(part, headerOffs, SEEK_SET) != 0) {
			return -1;
		}

		if (fwrite_workaround(&newheader, sizeof(newheader), 1, part) == 0) {
			return -1;
		}

		for (j = 0; j < oldheader->filecnt; ++j) {
			if (fseek(part, headerOffs + ((j + 1) * HGRAIN), SEEK_SET) != 0) {
				return -1;
			}

			if (fwrite_workaround(&f[j], sizeof(*f), 1, part) == 0) {
				return -1;
			}
		}
	}

	return 0;
}


static int getRecord(FILE *part, fileheader_t *file, entry_old_t *output, int pos)
{
	if (fseek(part, common.partOffset + RECORD_OLD_OFFS(file->sector, file->recordsz, pos), SEEK_SET) != 0) {
		return -1;
	}

	if (fread(output, file->recordsz + sizeof(entry_old_t), 1, part) == 0) {
		return -1;
	}

	return 0;
}


static int getRecordHeader(FILE *part, fileheader_t *file, entry_old_t *output, int pos)
{
	if (fseek(part, common.partOffset + RECORD_OLD_OFFS(file->sector, file->recordsz, pos), SEEK_SET) != 0) {
		return -1;
	}

	if (fread(output, sizeof(entry_old_t), 1, part) == 0) {
		return -1;
	}

	return 0;
}


static int writeRecord(FILE *part, uint32_t sector, entry_t *input, size_t recordsz, int pos)
{
	if (fseek(part, common.partOffset + RECORD_OFFS(sector, recordsz, pos), SEEK_SET) != 0) {
		return -1;
	}

	if (fwrite_workaround(input, recordsz + sizeof(entry_t), 1, part) == 0) {
		return -1;
	}

	return 0;
}


static int updateRecords(FILE *part, fileheader_t *file)
{
	entry_t *record;
	entry_old_t *oldrecord;
	int recordcnt = (int)(file->filesz / file->recordsz);
	int oldrecordmax = (file->sectorcnt * SIZE_SECTOR) / (file->recordsz + sizeof(entry_old_t));
	int olidx = -1, i, olpos = 0, ofpos;
	int sectorsNeeded;

	record = malloc(file->recordsz + sizeof(entry_t));
	oldrecord = malloc(file->recordsz + sizeof(entry_old_t));
	if (record == NULL || oldrecord == NULL) {
		free(record);
		free(oldrecord);
		return -1;
	}

	/* Find latest record */
	for (i = 0; i < oldrecordmax; ++i) {
		if (getRecordHeader(part, file, oldrecord, i) < 0) {
			free(record);
			free(oldrecord);
			return -1;
		}

		if (oldrecord->id.nvalid != 0) {
			continue;
		}

		if (oldrecord->id.no > olidx) {
			olidx = oldrecord->id.no;
			olpos = i;
		}
	}

	if (olidx < 0) {
		/* No records found, nothing to do */
		free(record);
		free(oldrecord);
		return 0;
	}

	/* Find first record */
	ofpos = olpos;
	if (recordcnt != 1) {
		do {
			--ofpos;
			if (ofpos < 0) {
				ofpos = oldrecordmax - 1;
			}

			if (getRecordHeader(part, file, oldrecord, ofpos) < 0) {
				free(record);
				free(oldrecord);
				return -1;
			}

			if ((oldrecord->id.nvalid != 0) || (olidx - oldrecord->id.no == recordcnt - 1)) {
				++ofpos;
				if (ofpos == oldrecordmax) {
					ofpos = 0;
				}
				break;
			}
		} while (ofpos != olpos);
	}

	/* Prepare temporary space */
	sectorsNeeded = ((recordcnt * (sizeof(entry_t) + file->recordsz)) + SIZE_SECTOR - 1) / SIZE_SECTOR;
	for (i = 0; i < sectorsNeeded; ++i) {
		if (eraseSector(part, common.freeSector + i) < 0) {
			free(record);
			free(oldrecord);
			return -1;
		}
	}

	/* Prepare converted records */
	for (i = 0; i < recordcnt; ++i) {
		if (getRecord(part, file, oldrecord, ofpos) < 0) {
			free(record);
			free(oldrecord);
			return -1;
		}

		record->id = oldrecord->id;
		record->checksum = 0;
		memcpy(record->data, oldrecord->data, file->recordsz);
		record->checksum = calcChecksum(record->data, file->recordsz);

		if (writeRecord(part, common.freeSector, record, file->recordsz, i) < 0) {
			free(record);
			free(oldrecord);
			return -1;
		}

		if (ofpos == olpos) {
			break;
		}

		++ofpos;
		if (ofpos >= oldrecordmax) {
			ofpos = 0;
		}
	}

	free(record);
	free(oldrecord);

	/* Erase original file */
	for (i = 0; i < file->sectorcnt; ++i) {
		if (eraseSector(part, file->sector + i) < 0) {
			return -1;
		}
	}

	/* Copy converted records to the original file */
	for (i = 0; i < sectorsNeeded; ++i) {
		if (copySector(part, file->sector + i, common.freeSector + i) < 0) {
			return -1;
		}
	}

	return 0;
}


int main(int argc, char *argv[])
{
	header_old_t oldheader;
	fileheader_t *files; /* array */
	FILE *part;
	int whichHeader, i;
	char *endptr;

	/* Arg check */
	if (argc < 2 || argc > 3) {
		fprintf(stderr, "Usage: %s PATH [offset]\n", argv[0]);
		return EXIT_FAILURE;
	}

	/* Open device */
	part = fopen(argv[1], "r+");
	if (part == NULL) {
		fprintf(stderr, "Could not open %s\n", argv[1]);
		return EXIT_FAILURE;
	}

	printf("Device opened\n");

	if (argc == 3) {
		common.partOffset = strtol(argv[2], &endptr, 0);

		if (common.partOffset < 0 || *endptr != '\0') {
			fprintf(stderr, "Invalid partition offset\n");
			(void)fclose(part);
			return EXIT_FAILURE;
		}
	}
	else {
		common.partOffset = 0;
	}

	/* Get old partition header */
	whichHeader = getOldHeader(part, &oldheader);
	if (whichHeader < 0) {
		fprintf(stderr, "Could not read old header\n");
		(void)fclose(part);
		return EXIT_FAILURE;
	}

	if (memcmp(oldheader.magic, oldMagicConst, sizeof(oldMagicConst)) != 0) {
		fprintf(stderr, "Old header magic mismatch. FS already converted?\n");
		(void)fclose(part);
		return EXIT_FAILURE;
	}

	printf("Got fs header: id %u, fcnt %u\n", oldheader.id.no, oldheader.filecnt);

	/* Check if partition has any files */
	if (oldheader.filecnt == 0) {
		printf("Found 0 files, nothing to do\n");
		(void)fclose(part);
		return 0;
	}

	/* Get file headers */
	files = malloc(sizeof(*files) * oldheader.filecnt);

	if (files == NULL) {
		(void)fclose(part);

		fprintf(stderr, "Out of memory\n");
		return EXIT_FAILURE;
	}

	if (getOldFiles(part, whichHeader, oldheader.filecnt, files) < 0) {
		free(files);
		(void)fclose(part);

		fprintf(stderr, "Failed to fetch old files headers\n");
		return EXIT_FAILURE;
	}

	printf("Files fetched. Relocating data\n");

	/* Move all files 2 sectors up to free 2 reserved sectors */
	if (moveSectors(part, files[0].sector, files[oldheader.filecnt - 1].sector + files[oldheader.filecnt - 1].sectorcnt, 2) < 0) {
		free(files);
		(void)fclose(part);

		fprintf(stderr, "Failed to relocate data\n");
		return EXIT_FAILURE;
	}

	for (i = 0; i < oldheader.filecnt; ++i) {
		files[i].sector += 2;
	}

	if (eraseSector(part, 2 * HEADER_SECTOR_CNT) < 0) {
		free(files);
		(void)fclose(part);

		fprintf(stderr, "Failed to erase reserved sector #1\n");
		return EXIT_FAILURE;
	}

	if (eraseSector(part, 2 * HEADER_SECTOR_CNT + 1) < 0) {
		free(files);
		(void)fclose(part);

		fprintf(stderr, "Failed to erase reserved sector #2\n");
		return EXIT_FAILURE;
	}

	printf("Reserved sectors are prepared. Realocating files\n");

	/* Allocate additional sectors if needed */
	for (i = oldheader.filecnt - 1; i >= 0; --i) {
		printf("File %s: old sectorcnt %u", files[i].name, files[i].sectorcnt);

		if (realocateFile(part, i, oldheader.filecnt, files) < 0) {
			free(files);
			(void)fclose(part);

			fprintf(stderr, "\nFailed to realocate file\n");
			return EXIT_FAILURE;
		}

		printf(", new sectorcnt %u\n", files[i].sectorcnt);
	}

	printf("Files reallocation done. Updating the fs header\n");

	/* Store new partition header and file headers */
	if (updateHeader(part, &oldheader, files) < 0) {
		free(files);
		(void)fclose(part);

		fprintf(stderr, "Failed to update partition header\n");
		return EXIT_FAILURE;
	}

	common.freeSector = files[oldheader.filecnt - 1].sector + files[oldheader.filecnt - 1].sectorcnt;

	printf("Header update done. Record converting\n");

	/* Add checksum to records in files */
	for (i = 0; i < oldheader.filecnt; ++i) {
		printf("Converting record of file %s\n", files[i].name);

		if (updateRecords(part, &files[i]) < 0) {
			free(files);
			(void)fclose(part);

			fprintf(stderr, "Failed to convert\n");
			return EXIT_FAILURE;
		}
	}

	/* We're done */
	free(files);
	(void)fclose(part);

	printf("Device closed. Done\n");

	return 0;
}
