/*
 * Phoenix-RTOS
 *
 * Rhealstone benchmark
 *
 * Common functions
 *
 * Copyright 2025 Phoenix Systems
 * Author: Lukasz Leczkowski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include "bench_common.h"

#include <inttypes.h>
#include <stdio.h>


uint64_t bench_printResult(uint64_t start, uint64_t end, int loops, uint64_t loopOverhead, uint64_t singleOverhead)
{
	uint64_t elapsed = end - start - loopOverhead;
	uint64_t time = (elapsed / loops) - singleOverhead;
	printf("Result: %" PRIu64 " cycles\n", time);

	return elapsed;
}


uint64_t bench_mutexLockOverhead(handle_t mutex)
{
	const int loops = 100;
	uint64_t start, end, total = 0;

	for (int i = 0; i < loops; i++) {
		start = bench_plat_getTime();
		mutexLock(mutex);
		end = bench_plat_getTime();
		mutexUnlock(mutex);
		total += end - start;
	}

	return total / loops;
}
