/*
 * Phoenix-RTOS
 *
 * Rhealstone benchmark
 *
 * Preemption
 *
 * Copyright 2025 Phoenix Systems
 * Author: Lukasz Leczkowski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <stdatomic.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/threads.h>
#include <sys/minmax.h>
#include <stdlib.h>

#include "bench_common.h"

#define MAX_LOOPS 4000

static struct {
	uint32_t one_tick;
	uint32_t one_tick_avg;
	atomic_int delay;
	uint64_t start1, start2, end1, end2;
	unsigned char stack[2][4096] __attribute__((aligned(8)));
} common;


/* Function for testing if one_tick_avg is set correctly. This is not part of the benchmark. */
void one_tick_timing_test(void)
{
	for (int i = 0; i < 10; i++) {
		uint64_t start1 = bench_plat_getTime();
		for (common.delay = 0; common.delay < common.one_tick_avg * 1000; common.delay++) {
			__asm__ volatile("nop");
			/* Delay loop */
		}

		uint64_t end1 = bench_plat_getTime();
		printf("Ticks per loop*1000: %u\n", (unsigned int)(end1 - start1));
	}
}


/* Low priority */
static void task1(void *arg)
{
	common.start1 = bench_plat_getTime();
	for (volatile int cnt1 = 0; cnt1 < MAX_LOOPS; cnt1++) {
		for (common.delay = 0; common.delay < common.one_tick; common.delay++) {
			__asm__ volatile("nop");
			/* Delay loop */
		}
	}
	common.end1 = bench_plat_getTime();
	endthread();
}

/* High priority */
static void task2(void *arg)
{
	common.start2 = bench_plat_getTime();
	for (volatile int cnt2 = 0; cnt2 < MAX_LOOPS; cnt2++) {
		common.delay = common.one_tick;
		usleep(1000);
	}
	common.end2 = bench_plat_getTime();
	endthread();
}


int main(int argc, char *argv[])
{
	puts("Rhealstone benchmark suite:\nPreemption");

	if (bench_plat_initTimer() < 0) {
		puts("Platform timer init fail");
		return 1;
	}

	priority(1);
#if 0
	one_tick_timing_test();
#else
	(void)one_tick_timing_test;
#endif

	common.one_tick_avg = bench_plat_getOneTickAvg();
	common.one_tick = bench_plat_getOneTick();
	uint64_t overhead = bench_plat_getTime();

	for (volatile int cnt1 = 0; cnt1 < MAX_LOOPS; cnt1++) {
		for (atomic_int i = 0; i < common.one_tick_avg; i++) {
			__asm__ volatile("nop");
			/* Delay loop */
		}
	}
	for (int cnt2 = 0; cnt2 < MAX_LOOPS; cnt2++) {
		__asm__ volatile("nop");
	}

	overhead = bench_plat_getTime() - overhead;

	int tid1, tid2;
	int res = beginthreadex(task1, 3, common.stack[0], sizeof(common.stack[0]), NULL, &tid1);
	if (res < 0) {
		puts("beginthreadex fail");
		return -1;
	}

	res = beginthreadex(task2, 2, common.stack[1], sizeof(common.stack[1]), NULL, &tid2);
	if (res < 0) {
		puts("beginthreadex fail");
		return -1;
	}

	priority(4);

	usleep(0);

	threadJoin(tid1, 0);
	threadJoin(tid2, 0);

	bench_printResult(min(common.start1, common.start2), max(common.end1, common.end2), 2 * MAX_LOOPS, overhead, 0);

	return 0;
}
