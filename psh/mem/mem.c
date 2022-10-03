/*
 * Phoenix-RTOS
 *
 * mem - view process or kernel memory map
 *
 * Copyright 2017, 2018, 2020, 2021 Phoenix Systems
 * Author: Pawel Pisarczyk, Jan Sikorski, Maciej Purski, Mateusz Niewiadomski
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

#include "../psh.h"


static int psh_mem_summary(void)
{
	meminfo_t info;

	memset(&info, 0, sizeof(info));

	info.page.mapsz = -1;
	info.entry.mapsz = -1;
	info.entry.kmapsz = -1;
	info.maps.mapsz = -1;

	meminfo(&info);
	printf("(%d+%d)/%dKB ", (info.page.alloc - info.page.boot) / 1024, info.page.boot / 1024, (info.page.alloc + info.page.free) / 1024);
	printf("%d/%d entries\n", info.entry.total - info.entry.free, info.entry.total);

	return 0;
}


static void psh_mem_procprint(entryinfo_t *e, int mapsz)
{
	char flags[5], prot[4];
	unsigned int i;

	printf("%-*s  PROT  FLAGS  %*s  OBJECT\n", (int)(4 * sizeof(e->vaddr) + 1), "SEGMENT", (int)(2 * sizeof(e->offs)), "OFFSET");

	e += mapsz - 1;
	for (i = 0; i < mapsz; ++i, --e) {
		memset(flags, ' ', sizeof(flags));
		if (e->flags & MAP_NEEDSCOPY)
			flags[0] = 'C';
		if (e->flags & MAP_PRIVATE)
			flags[1] = 'P';
		if (e->flags & MAP_FIXED)
			flags[2] = 'F';
		if (e->flags & MAP_ANONYMOUS)
			flags[3] = 'A';
		flags[4] = '\0';

		memset(prot, '-', sizeof(prot));
		if (e->prot & PROT_READ)
			prot[0] = 'r';
		if (e->prot & PROT_WRITE)
			prot[1] = 'w';
		if (e->prot & PROT_EXEC)
			prot[2] = 'x';
		prot[3] = '\0';

		printf("%p:%p  %-4s  %-5s", e->vaddr, e->vaddr + e->size - 1, prot, flags);

		if (e->offs != -1)
			printf("  %*llx", (int)(2 * sizeof(e->offs)), e->offs);
		else
			printf("  %*s", (int)(2 * sizeof(e->offs)), "");

		if (e->object == OBJECT_ANONYMOUS)
			printf("  %s", "(anonymous)");
		else if (e->object == OBJECT_MEMORY)
			printf("  %s", "mem");
		else
			printf("  %d.%llu", e->oid.port, (unsigned long long)e->oid.id);

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
				fprintf(stderr, "mem: out of memory\n");
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
				fprintf(stderr, "mem: could not parse process id: '%s'\n", memarg);
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
				fprintf(stderr, "mem: out of memory\n");
				free(info.page.map);
				return -ENOMEM;
			}
			info.entry.map = rmap;
			meminfo(&info);
		}
		while (info.entry.mapsz > mapsz);

		if (info.entry.mapsz < 0) {
			fprintf(stderr, "mem: process with pid %u not found\n", info.entry.pid);
			free(info.entry.map);
			return -EINVAL;
		}

		psh_mem_procprint(info.entry.map, info.entry.mapsz);
	}

	free(info.entry.map);
	free(info.entry.kmap);

	return 0;
}


static int psh_mem_page(void)
{
	int i, mapsz = 0;
	pageinfo_t *p = NULL;
	unsigned int n;
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
			fprintf(stderr, "mem: out of memory\n");
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
	if (info.page.mapsz < 0) {
		fprintf(stderr, "mem: Page view unavailable\n");
	}
	else {
		printf("\n");
	}

	free(info.page.map);
	return 0;
}


static void psh_meminfo(void)
{
	printf("prints memory map");
}


static int psh_help(const char *progname)
{
	printf("Usage: %s [OPTION]\n"
		"\t-m    process memory info\n"
		"\t-p    page info\n"
		"\t-s    shared memory maps info\n"
		"\t-h    help\n", progname);

	return 0;
}


static void psh_bytes2humanReadable(char *buff, size_t buffsz, size_t bytes)
{
	static const char suf[4][4] = { "B", "KiB", "MiB", "GiB" };
	int i;
	size_t tmp;

	for (i = 0; i < 3; ++i) {
		tmp = bytes / 1024;
		if (tmp == 0) {
			break;
		}
		bytes = tmp;
	}

	snprintf(buff, buffsz, "%zu %s", bytes, suf[i]);
}


static int psh_sharedMaps(void)
{
	meminfo_t info;
	int i;
	char buff[32];

	info.page.mapsz = -1;
	info.entry.kmapsz = -1;
	info.entry.mapsz = -1;
	info.maps.mapsz = 0;
	info.maps.map = NULL;

	meminfo(&info);
	if (info.maps.mapsz == 0) {
		printf("mem: no shared memory maps are present\n");
		return 0;
	}

	info.maps.map = malloc(info.maps.mapsz * sizeof(mapinfo_t));
	if (info.maps.map == NULL) {
		fprintf(stderr, "mem: out of memory\n");
		return -ENOMEM;
	}
	meminfo(&info);

	printf("All maps:\n");
	psh_bytes2humanReadable(buff, sizeof(buff), info.maps.total);
	printf("\tTotal: %s (%zu bytes)\n", buff, info.maps.total);
	psh_bytes2humanReadable(buff, sizeof(buff), info.maps.free);
	printf("\tFree:  %s (%zu bytes)\n", buff, info.maps.free);

	for (i = 0; i < info.maps.mapsz; ++i) {
		if (info.maps.map[i].alloc == 0 && info.maps.map[i].free == 0) {
			continue;
		}

		printf("\nMap #%d\n", info.maps.map[i].id);
		psh_bytes2humanReadable(buff, sizeof(buff), info.maps.map[i].alloc + info.maps.map[i].free);
		printf("\tSize:     %s (%zu bytes)\n", buff, info.maps.map[i].alloc + info.maps.map[i].free);
		psh_bytes2humanReadable(buff, sizeof(buff), info.maps.map[i].alloc);
		printf("\tAlloc:    %s (%zu bytes)\n", buff, info.maps.map[i].alloc);
		psh_bytes2humanReadable(buff, sizeof(buff), info.maps.map[i].free);
		printf("\tFree:     %s (%zu bytes)\n", buff, info.maps.map[i].free);
		printf("\tPhysical: 0x%p:0x%p\n", (void *)info.maps.map[i].pstart, (void *)info.maps.map[i].pend);
		printf("\tVirtual:  0x%p:0x%p\n", (void *)info.maps.map[i].vstart, (void *)info.maps.map[i].vend);
	}

	free(info.maps.map);

	return 0;
}


static int psh_mem(int argc, char **argv)
{
	int c;

	if (argc == 1)
		return psh_mem_summary() < 0 ? EXIT_FAILURE : EXIT_SUCCESS;

	while ((c = getopt(argc, argv, "mpsh")) != -1) {
		switch (c) {
		case 'm':
			return psh_mem_process(argv[optind]) < 0 ? EXIT_FAILURE : EXIT_SUCCESS;

		case 'p':
			return psh_mem_page() < 0 ? EXIT_FAILURE : EXIT_SUCCESS;

		case 'h':
			return psh_help(argv[0]) < 0 ? EXIT_FAILURE : EXIT_SUCCESS;

		case 's':
			return psh_sharedMaps() < 0 ? EXIT_FAILURE : EXIT_SUCCESS;

		default:
			return EXIT_FAILURE;
		}
	}

	if (optind < argc) {
		fprintf(stderr, "mem: unknown argument: %s\n", argv[optind]);
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}


void __attribute__((constructor)) mem_registerapp(void)
{
	static psh_appentry_t app = {.name = "mem", .run = psh_mem, .info = psh_meminfo};
	psh_registerapp(&app);
}
