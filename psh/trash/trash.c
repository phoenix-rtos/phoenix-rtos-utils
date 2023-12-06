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

#define DATA_SIZE (600 * 1024 * 1024) /* 600 MiB */
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
	int bigFile, smallFile;

	srand(time(NULL));
	bigFile = open("/data/bigFile", O_WRONLY | O_CREAT);
	if (bigFile < 0) {
		perror("open(bigFile)");
		return EXIT_FAILURE;
	}
	for (i = 0, blocksToWrite= DATA_SIZE / GARBAGE_SIZE; blocksToWrite> 0; blocksToWrite-= 2, i += 1) {
		sprintf(name, "/data/smallFile%u", i);
		smallFile = open(name, O_WRONLY | O_CREAT);
		if (smallFile < 0) {
			perror("open(smallFile)");
			close(bigFile);
			return EXIT_FAILURE;
		}
		generateGarbage();
		write(bigFile, garbage, sizeof(garbage));
		generateGarbage();
		write(smallFile, garbage, sizeof(garbage));
		sync();
		close(smallFile);
		if (i % 100 == 0) printf("%u/%u\n", 2 * i * GARBAGE_SIZE, DATA_SIZE);
	}

	close(bigFile);
	return 0;
}

static void printTrash(void)
{
	printf("top utility");
}

void __attribute__((constructor)) trash_registerapp(void)
{
	static psh_appentry_t app = {.name = "trash", .run = trash, .info = printTrash};
	psh_registerapp(&app);
}
