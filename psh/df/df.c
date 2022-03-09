/*
 * Phoenix-RTOS
 *
 * df - print filesystem statistics
 *
 * Copyright 2022 Phoenix Systems
 * Author: Lukasz Kosinski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/statvfs.h>

#include "../psh.h"


static void psh_df_info(void)
{
	printf("print filesystem statistics");
}


static void psh_df_help(const char *prog)
{
	printf("Usage: %s [options] [files]\n", prog);
	printf("  -T:  print filesystem type\n");
	printf("  -i:  print inode information instead of block usage\n");
	printf("  -h:  print this help message\n");
}


static int psh_df(int argc, char **argv)
{
	int i, c, type = 0, inodes = 0, unit = 1024, ret = EXIT_SUCCESS;
	struct statvfs st;
	fsblkcnt_t used;

	while ((c = getopt(argc, argv, "Tih")) != -1) {
		switch (c) {
			case 'T':
				type = 1;
				break;

			case 'i':
				inodes = 1;
				break;

			case 'h':
				psh_df_help(argv[0]);
				return EXIT_SUCCESS;

			default:
				psh_df_help(argv[0]);
				return EXIT_FAILURE;
		}
	}

	if (optind == argc) {
		printf("df: no mount table support\n");
		return EXIT_FAILURE;
	}

	printf("Filesystem     %s %-9s      Used Available Capacity Mounted on\n",
		(type) ? " Type      " : "",
		(inodes) ? "   Inodes" : "1K-blocks");

	for (i = optind; i < argc; ++i) {
		if (statvfs(argv[i], &st) < 0) {
			fprintf(stderr, "df: %s: no such file or directory\n", argv[i]);
			ret = EXIT_FAILURE;
			continue;
		}

		if (inodes) {
			st.f_blocks = st.f_files;
			st.f_bavail = st.f_bfree = st.f_ffree;
			st.f_frsize = 1;
			unit = 1;
		}
		used = st.f_blocks - st.f_bfree;

		/* TODO: get mounted device and fs type from mount table */
		printf("%-15s", "device");
		if (type) {
			printf(" %-10s", "fs");
		}
		printf(" %9llu %9llu %9llu %7llu%% %s\n",
			(st.f_blocks * st.f_frsize + unit / 2) / unit,
			((st.f_blocks - st.f_bfree) * st.f_frsize + unit / 2) / unit,
			(st.f_bavail * st.f_frsize + unit / 2) / unit,
			(used + st.f_bavail) ? (100 * used + (used + st.f_bavail) / 2) / (used + st.f_bavail) : 0ULL,
			argv[i]); /* TODO: get mount point from mount table */
	}

	return ret;
}


void __attribute__((constructor)) df_registerapp(void)
{
	static psh_appentry_t app = { .name = "df", .run = psh_df, .info = psh_df_info };
	psh_registerapp(&app);
}
