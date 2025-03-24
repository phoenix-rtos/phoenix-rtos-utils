/*
 * Phoenix-RTOS
 *
 * Rhealstone benchmark
 *
 * Deadlock breaking
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
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/threads.h>

#include <board_config.h>
#include "bench_common.h"


#define BENCHMARKS 5000


static struct {
	bool deadBrk;
	handle_t mutex;

	unsigned char stack[3][4096] __attribute__((aligned(8)));

	atomic_bool done;
	atomic_bool t3_started;

	uint64_t totalNoDeadBrk;
	uint64_t totalDeadBrk;
} common = {
	.done = false,
	.t3_started = false,
};


/* High priority task */
static void task3(void *arg)
{
	common.t3_started = true;
	uint64_t time = bench_getTime();
	if (common.deadBrk) {
		mutexLock(common.mutex);
	}

	if (common.deadBrk) {
		mutexUnlock(common.mutex);
		time = bench_getTime() - time;
		common.totalDeadBrk += time;
	}
	else {
		time = bench_getTime() - time;
		common.totalNoDeadBrk += time;
	}
	common.done = true;
	endthread();
}


/* Medium priority task */
static void task2(void *arg)
{
	int tid3;
	if (beginthreadex(task3, 1, common.stack[2], sizeof(common.stack[2]), NULL, &tid3)) {
		puts("beginthreadex fail");
		endthread();
	}

	while (!common.done) {
		__asm__ volatile("nop");
	}

	threadJoin(tid3, 0);

	endthread();
}

/* Low priority task */
static void task1(void *arg)
{
	int tid2;
	if (common.deadBrk) {
		mutexLock(common.mutex);
	}

	if (beginthreadex(task2, 2, common.stack[1], sizeof(common.stack[1]), NULL, &tid2)) {
		puts("beginthreadex fail");
		endthread();
	}

	while (!common.t3_started) {
		usleep(0);
	}

	if (common.deadBrk) {
		mutexUnlock(common.mutex);
	}

	threadJoin(tid2, 0);
	common.t3_started = false;
	endthread();
}


static int doTest(void)
{
	int tid1;
	if (beginthreadex(task1, 3, common.stack[0], sizeof(common.stack[0]), NULL, &tid1)) {
		puts("beginthreadex fail");
		return -1;
	}

	priority(4);

	usleep(0);

	threadJoin(tid1, 0);

	priority(0);

	return 0;
}


int main(int argc, char *argv[])
{
	puts("Rhealstone benchmark suite:\nDeadlock breaking");

	priority(0);

	mutexCreate(&common.mutex);

	uint64_t mutexOverhead = bench_mutexLockOverhead(common.mutex);

	common.deadBrk = false;


	for (int i = 0; i < BENCHMARKS; i++) {
		if (doTest() < 0) {
			return -1;
		}
	}

	common.deadBrk = true;

	for (int i = 0; i < BENCHMARKS; i++) {
		if (doTest() < 0) {
			return -1;
		}
	}

	printf("Deadlocks: per resolution\n");
	bench_printResult(0, common.totalDeadBrk, BENCHMARKS, common.totalNoDeadBrk, mutexOverhead);

	return 0;
}
