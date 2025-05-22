/*
 * Phoenix-RTOS
 *
 * Rhealstone benchmark
 *
 * Task switch
 *
 * Copyright 2025 Phoenix Systems
 * Author: Lukasz Leczkowski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/threads.h>
#include <sys/minmax.h>

#include "bench_common.h"


#define MAX_LOOPS 400000


static struct {
	uint64_t start1, start2, end1, end2;
	unsigned char stack[2][4096] __attribute__((aligned(8)));
} common;


static void task1(void *arg)
{
	common.start1 = bench_plat_getTime();
	for (unsigned int i = 0; i < MAX_LOOPS; i++) {
		usleep(0);
	}
	common.end1 = bench_plat_getTime();
	endthread();
}


static void task2(void *arg)
{
	common.start2 = bench_plat_getTime();
	for (unsigned int i = 0; i < MAX_LOOPS; i++) {
		usleep(0);
	}
	common.end2 = bench_plat_getTime();
	endthread();
}


int main(int argc, char *argv[])
{
	puts("Rhealstone benchmark suite:\nTask Switching");

	if (bench_plat_initTimer() < 0) {
		puts("Platform timer init fail");
		return 1;
	}

	priority(1);

	uint64_t overhead = bench_plat_getTime();

	for (unsigned int i = 0; i < MAX_LOOPS; i++) {
		__asm__ volatile("nop");
	}
	for (unsigned int i = 0; i < MAX_LOOPS; i++) {
		__asm__ volatile("nop");
	}

	overhead = bench_plat_getTime() - overhead;

	int tid1, tid2;
	int res = beginthreadex(task1, 2, common.stack[0], sizeof(common.stack[0]), NULL, &tid1);
	if (res < 0) {
		puts("beginthreadex fail");
		return -1;
	}

	res = beginthreadex(task2, 2, common.stack[1], sizeof(common.stack[1]), NULL, &tid2);
	if (res < 0) {
		puts("beginthreadex fail");
		return -1;
	}

	priority(3);

	usleep(0);

	threadJoin(tid1, 0);
	threadJoin(tid2, 0);

	bench_printResult(min(common.start1, common.start2), max(common.end1, common.end2), 2 * MAX_LOOPS, overhead, 0);

	return 0;
}
