#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "../psh.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define DATA_SIZE (1500 * 1024 * 1024) /* 1500 MiB */
#define GARBAGE_SIZE (8*1024)

char garbage[GARBAGE_SIZE];

static void generateGarbage(void)
{
	size_t i;
	for (i = 0; i < sizeof(garbage); ++i) {
		garbage[i] = rand();
	}
}

static int trash(void)
{
	char name[32];
	size_t blocksToWrite, i;
	int bigFile;

	srand(time(NULL));
	bigFile = open("/data/bigFile", O_WRONLY | O_CREAT);
	if (bigFile < 0) {
		perror("open(bigFile)");
		return EXIT_FAILURE;
	}
	for (i = 0, blocksToWrite= DATA_SIZE / GARBAGE_SIZE; blocksToWrite> 0; blocksToWrite-= 2, i += 1) {
		generateGarbage();
		write(bigFile, garbage, sizeof(garbage));
		sync();
		if (i % 100 == 0) printf("%u/%u\n", 2 * i * GARBAGE_SIZE, DATA_SIZE);
	}

	close(bigFile);
	return 0;
}


char SQLBlock[1024 * 1024]; /* 1 MiB */

#define SMALL_FILE_COUNT 100

static int trashSql(int argc, char *argv[])
{
	printf("TRASH SQL\n\n");
	char buffer[250];
	int file;
	int smallFile[SMALL_FILE_COUNT];
	size_t n = atoi(argv[1]), m = atoi(argv[2]);
	if (n == 0 || m == 0) {
		fprintf(stderr, "invalid\n");
		return 1;
	}

	srand(time(NULL));

	file = open("/data/trashSQL", O_WRONLY | O_CREAT, 0777);
	if (file < 0) {
		perror("open(file)");
		return EXIT_FAILURE;
	}
	for (size_t i = 0; i < SMALL_FILE_COUNT; ++i) {
		sprintf(buffer, "/data/trashSQL%u", i % SMALL_FILE_COUNT);
		smallFile[i] = open(buffer, O_WRONLY | O_CREAT, 0777);
		if (smallFile < 0) {
			perror("open(smallFile)");
			return EXIT_FAILURE;
		}
	}
	/* Filling */
	for (size_t i = 0; i < n; ++i) {
		for (size_t j = 0; j < sizeof(SQLBlock); ++j) {
			SQLBlock[j] = rand();
		}
		if (write(file, SQLBlock, sizeof(SQLBlock)) == -1) {
			perror("write 1");
			return 1;
		}
		if (i % 10 == 0) printf("%zu/%zu\n", i, n);
	}	
	sync();
	/* Random accesses */
	for (size_t i = 0; i < m; ++i) {
		const size_t size = 1024 * (((size_t)rand()) % 1024);
		for (size_t j = 0; j < size; ++j) {
			SQLBlock[j] = rand();
		}
		if (lseek(file, rand() % (n * 1024), SEEK_SET) == -1) {
			perror("lseek 1");
			return 1;
		}
		if (write(file, SQLBlock, size) == -1) {
			perror("write 2");
			return 1;
		}

		if (lseek(smallFile[i % SMALL_FILE_COUNT], 0, SEEK_SET) == (off_t)-1) {
			perror("lseek 2");
			return 1;
		}
		if (write(smallFile[i % SMALL_FILE_COUNT], SQLBlock, size) == -1) {
			perror("write 3");
			return 1;
		}
		sync();
		if (i % 10 == 0) {
		 	printf("%zu/%zu\n", i, m);
		}
	}

	close(file);
	for (size_t i = 0; i < SMALL_FILE_COUNT; ++i) {
		close(smallFile[i]);
	}
	return 0;
}

static void printTrash(void)
{
	printf("top utility");
}

void __attribute__((constructor)) trash_registerapp(void)
{
	static psh_appentry_t appTrash = {.name = "trash", .run = trash, .info = printTrash};
	static psh_appentry_t appTrashSql = {.name = "trashSql", .run = trashSql, .info = printTrash};
	psh_registerapp(&appTrash);
	psh_registerapp(&appTrashSql);
}
