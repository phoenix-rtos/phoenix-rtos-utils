/*
 * Phoenix-RTOS
 *
 * mem - view process or kernel memory map
 *
 * Copyright 2017, 2018, 2020 Phoenix Systems
 * Author: Pawel Pisarczyk, Jan Sikorski, Maciej Purski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/threads.h>


static int psh_mem_summary(void)
{
	meminfo_t info;

	memset(&info, 0, sizeof(info));

	info.page.mapsz = -1;
	info.entry.mapsz = -1;
	info.entry.kmapsz = -1;

	meminfo(&info);
	printf("(%d+%d)/%dKB ", (info.page.alloc - info.page.boot) / 1024, info.page.boot / 1024, (info.page.alloc + info.page.free) / 1024);
	printf("%d/%d entries\n", info.entry.total - info.entry.free, info.entry.total);

	return EOK;
}


static void psh_mem_procprint(entryinfo_t *e, int mapsz)
{
	char flags[8], prot[5];
	unsigned int i, j;

	printf("%-17s  PROT  FLAGS  %16s  OBJECT\n", "SEGMENT", "OFFSET");

	e += mapsz - 1;
	for (i = 0; i < mapsz; ++i, --e) {

		j = 0;
		if (e->flags & MAP_NEEDSCOPY)
			flags[j++] = 'C';
		if (e->flags & MAP_PRIVATE)
			flags[j++] = 'P';
		if (e->flags & MAP_FIXED)
			flags[j++] = 'F';
		if (e->flags & MAP_ANONYMOUS)
			flags[j++] = 'A';
		flags[j] = '\0';

		memset(prot, '-', sizeof(prot));

		if (e->prot & PROT_READ)
			prot[0] = 'r';
		if (e->prot & PROT_WRITE)
			prot[1] = 'w';
		if (e->prot & PROT_EXEC)
			prot[2] = 'x';
		prot[4] = '\0';

		printf("%p:%p  %4s  %5s", e->vaddr, e->vaddr + e->size - 1, prot, flags);

		if (e->offs != -1)
			printf("  %16llx", e->offs);
		else
			printf("  %16s", "");

		if (e->object == OBJECT_ANONYMOUS)
			printf("  %s", "(anonymous)");
		else if (e->object == OBJECT_MEMORY)
			printf("  %s", "mem");
		else
			printf("  %d.%llu", e->oid.port, e->oid.id);

		if (e->object != OBJECT_ANONYMOUS && e->anonsz != ~0)
			printf("/(%zuKB)", e->anonsz / 1024);

		printf("\n");
	}

	return;
}


static int psh_mem_process(char *memarg)
{
	char *end;
	int mapsz = 0;
	meminfo_t info;
	void *rmap;

	memset(&info, 0, sizeof(info));

	info.page.mapsz = -1;

	if (memarg && !strcmp("kernel", memarg))  {
		/* Show memory map of the kernel */
		info.entry.mapsz = -1;
		info.entry.kmapsz = 16;

		do {
			mapsz = info.entry.kmapsz;
			if ((rmap = realloc(info.entry.kmap, mapsz * sizeof(entryinfo_t))) == NULL) {
				printf("mem: out of memory\n");
				free(info.entry.kmap);
				return -ENOMEM;
			}
			info.entry.kmap = rmap;
			meminfo(&info);
		}
		while (info.entry.kmapsz > mapsz);

		psh_mem_procprint(info.entry.kmap, info.entry.kmapsz);
	}
	else {
		/* Show memory map of a process */
		if (memarg) {
			info.entry.pid = strtoul(memarg, &end, 10);

			if (*end != '\0') {
				printf("mem: could not parse process id: '%s'\n", memarg);
				return -EINVAL;
			}
		}
		else {
			info.entry.pid = getpid();
		}

		info.entry.kmapsz = -1;
		info.entry.mapsz = 16;

		do {
			mapsz = info.entry.mapsz;
			if ((rmap = realloc(info.entry.map, mapsz * sizeof(entryinfo_t))) == NULL) {
				printf("mem: out of memory\n");
				free(info.page.map);
				return -ENOMEM;
			}
			info.entry.map = rmap;
			meminfo(&info);
		}
		while (info.entry.mapsz > mapsz);

		if (info.entry.mapsz < 0) {
			printf("mem: process with pid %u not found\n", info.entry.pid);
			free(info.entry.map);
			return -EINVAL;
		}

		psh_mem_procprint(info.entry.map, info.entry.mapsz);
	}

	free(info.entry.map);
	free(info.entry.kmap);

	return EOK;
}


static int psh_mem_page(void)
{
	int mapsz = 0;
	pageinfo_t *p = NULL;
	unsigned int i, n;
	meminfo_t info;
	void *rmap;

	memset(&info, 0, sizeof(info));

	/* Show page map */
	info.entry.mapsz = ~0;
	info.entry.kmapsz = ~0;
	info.page.mapsz = 16;

	do {
		mapsz = info.page.mapsz;
		if ((rmap = realloc(info.page.map, mapsz * sizeof(pageinfo_t))) == NULL) {
			printf("psh: out of memory\n");
			free(info.page.map);
			return -ENOMEM;
		}
		info.page.map = rmap;
		meminfo(&info);
	}
	while (info.page.mapsz > mapsz);

	for (i = 0, p = info.page.map; i < info.page.mapsz; ++i, ++p) {
		if ((p != info.page.map) && (n = (p->addr - (p - 1)->addr) / _PAGE_SIZE - (p - 1)->count)) {
			if (n > 3) {
				printf("[%ux]", n);
			}
			else {
				while (n-- > 0)
					printf("x");
			}
		}

		if ((n = p->count) > 3) {
			printf("[%u%c]", p->count, p->marker);
			continue;
		}

		while (n-- > 0)
			printf("%c", p->marker);
	}
	printf("\n");

	free(info.page.map);
	return EOK;
}


int psh_mem(int argc, char **argv)
{
	int c;

	if (argc == 1)
		return psh_mem_summary();

	while ((c = getopt(argc, argv, "mp")) != -1) {
		switch (c) {
		case 'm':
			return psh_mem_process(argv[optind]);
		case 'p':
			return psh_mem_page();
		default:
			return EOK;
		}
	}

	if (optind < argc) {
		printf("mem: unknown argument: %s\n", argv[optind]);
		return -1;
	}

	return EOK;
}
