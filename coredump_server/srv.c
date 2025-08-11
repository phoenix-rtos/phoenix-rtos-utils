/*
 * Phoenix-RTOS
 *
 * coredump server executable
 *
 * Copyright 2025 Phoenix Systems
 * Author: Jakub Klimek
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include "coredump.h"

#include <string.h>

#ifndef COREDUMP_MAX_THREADS
#define COREDUMP_MAX_THREADS 4
#endif

#ifndef COREDUMP_MAX_STACK_SIZE
#define COREDUMP_MAX_STACK_SIZE 0
#endif

#ifndef COREDUMP_MEM_SCOPE
#define COREDUMP_MEM_SCOPE COREDUMP_MEM_ALL_STACKS
#endif

#ifndef COREDUMP_FP_CONTEXT
#define COREDUMP_FP_CONTEXT 0
#endif

#ifndef COREDUMP_MAX_MEM_CHUNK
#define COREDUMP_MAX_MEM_CHUNK 0
#endif

#ifndef COREDUMP_PRINT
#define COREDUMP_PRINT 1
#endif

#ifndef COREDUMP_PRINT_SLEEP
#define COREDUMP_PRINT_SLEEP 10 * 1000 /* 10ms */
#endif

#ifndef COREDUMP_SAVEPATH
#define COREDUMP_SAVEPATH "/coredumps"
#endif

#ifndef COREDUMP_MAX_FILES
#define COREDUMP_MAX_FILES 0
#endif


static coredump_opts_t opts;


int main(int argc, char *argv[])
{
	opts = (coredump_opts_t) {
		.maxThreads = COREDUMP_MAX_THREADS,
		.maxStackSize = COREDUMP_MAX_STACK_SIZE,
		.memScope = COREDUMP_MEM_SCOPE,
		.fpContext = COREDUMP_FP_CONTEXT,
		.maxMemChunk = COREDUMP_MAX_MEM_CHUNK,
		.print = COREDUMP_PRINT,
		.printSleep = COREDUMP_PRINT_SLEEP,
		.savepath = COREDUMP_SAVEPATH,
		.maxFiles = COREDUMP_MAX_FILES,
	};
	if (argc >= 2 && strcmp(argv[1], "config") == 0) {
		return coredump_configure("config", argc - 2, &argv[2]);
	}
	else {
		int ret = coredump_parseStartupArgs(argc, argv, &opts);
		if (ret != 0) {
			return ret;
		}
	}
	coredump_serverthr(&opts);
	return 0;
}
