/*
 * Phoenix-RTOS
 *
 * Rhealstone benchmark
 *
 * Semaphore shuffling
 *
 * Copyright 2025 Phoenix Systems
 * Author: Lukasz Leczkowski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/threads.h>

#include "bench_common.h"


#define BENCHMARKS 50000


static struct {
	bool semExe;
	handle_t mutex;
	uint64_t overhead;
	atomic_int count;
	unsigned char stack[2][4096] __attribute__((aligned(8)));
} common = {
	.semExe = false,
};


static void task2(void *arg)
{
	uint64_t start, end;
	start = bench_plat_getTime();

	for (common.count = 0; common.count < BENCHMARKS; common.count++) {
		if (common.semExe) {
			mutexLock(common.mutex);
		}

		usleep(0);

		if (common.semExe) {
			mutexUnlock(common.mutex);
		}

		usleep(0);
	}
	end = bench_plat_getTime();

	if (!common.semExe) {
		common.overhead = end - start;
	}
	else {
		bench_printResult(start, end, BENCHMARKS, common.overhead, 0);
	}
	endthread();
}


static void task1(void *arg)
{
	int tid;
	if (beginthreadex(task2, 2, common.stack[1], sizeof(common.stack[1]), NULL, &tid)) {
		puts("beginthreadex fail");
		endthread();
	}

	usleep(0);
	for (; common.count < BENCHMARKS;) {
		if (common.semExe) {
			mutexLock(common.mutex);
		}

		usleep(0);

		if (common.semExe) {
			mutexUnlock(common.mutex);
		}

		usleep(0);
	}

	threadJoin(tid, 0);

	endthread();
}


int main(int argc, char *argv[])
{
	puts("Rhealstone benchmark suite:\nSemaphore shuffle");

	if (bench_plat_initTimer() < 0) {
		puts("Platform timer init fail");
		return 1;
	}

	priority(1);

	mutexCreate(&common.mutex);

	int tid1;
	if (beginthreadex(task1, 2, common.stack[0], sizeof(common.stack[0]), NULL, &tid1) < 0) {
		puts("beginthreadex fail");
		return -1;
	}

	priority(3);

	usleep(0);

	threadJoin(tid1, 0);

	priority(1);

	common.semExe = true;

	if (beginthreadex(task1, 2, common.stack[0], sizeof(common.stack[0]), NULL, &tid1) < 0) {
		puts("beginthreadex fail");
		return -1;
	}

	priority(3);

	usleep(0);

	threadJoin(tid1, 0);

	return 0;
}
