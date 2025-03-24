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


#if defined(__CPU_GR740)


uint64_t bench_getTime(void)
{
	uint32_t asr22, asr23;
	uint64_t cntr;
	__asm__ volatile(
			"rd %%asr22, %0\n\t"
			"rd %%asr23, %1"
			: "=r"(asr22), "=r"(asr23));

	cntr = ((uint64_t)(asr22 & 0xffffffu) << 32) | asr23;

	return cntr;
}


#else


uint64_t bench_getTime(void)
{
	/* TODO: implement per platform
	 * this function needs to work in interrupts!
	 */
	return 0;
}


#endif


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
		start = bench_getTime();
		mutexLock(mutex);
		end = bench_getTime();
		mutexUnlock(mutex);
		total += end - start;
	}

	return total / loops;
}
