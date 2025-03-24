/*
 * Phoenix-RTOS
 *
 * High load benchmark
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

#include <sys/threads.h>
#include <sys/time.h>

#include "bench_common.h"


#define THREAD_STACK_SIZE      1024
#define MAX_TASKS              1024
#define BENCHMARK_DURATION_SEC 10


static struct {
	uint8_t stack[MAX_TASKS][THREAD_STACK_SIZE] __attribute__((aligned(8)));
	unsigned int counters[MAX_TASKS];
	atomic_bool taskStart;
} common = {
	.taskStart = false,
};


void idleTask(void *arg)
{
	unsigned int n = (unsigned int)arg;
	while (!common.taskStart) {
		usleep(0);
	}
	uint64_t start_cycles = bench_getTime();
	uint64_t end_cycles = start_cycles + (BENCHMARK_DURATION_SEC * 250000000ULL);  // 250 MHz * 1 second

	while (bench_getTime() < end_cycles) {
		++common.counters[n];
	}

	endthread();
}


int doTest(void (*task)(void *), int ntasks, unsigned int sleepTimeSec)
{
	static int tid[MAX_TASKS];

	if (ntasks > MAX_TASKS) {
		ntasks = MAX_TASKS;
	}

	common.taskStart = false;

	for (int i = 0; i < ntasks; i++) {
		if (beginthreadex(task, 2, common.stack[i], sizeof(common.stack[i]), (void *)i, &tid[i]) < 0) {
			puts("beginthreadex fail");
			return -1;
		}
	}

	common.taskStart = true;

	usleep(sleepTimeSec * 1000 * 1000);

	for (int i = 0; i < ntasks; i++) {
		threadJoin(tid[i], 0);
	}

	return 0;
}


int main(int argc, char *argv[])
{
	int ntasks = MAX_TASKS;
	printf("Starting benchmark\n");

	if (argc > 1) {
		ntasks = atoi(argv[1]);
		if (ntasks > MAX_TASKS) {
			ntasks = MAX_TASKS;
			printf("Number of tasks limited to %d\n", MAX_TASKS);
		}
	}

	if (priority(0) < 0) {
		puts("priority fail");
		return -1;
	}

	if (doTest(idleTask, ntasks, BENCHMARK_DURATION_SEC) < 0) {
		return -1;
	}

	printf("High load benchmark results (%d tasks)\n", ntasks);
	for (int i = 0; i < ntasks; i++) {
		printf("%d%c", common.counters[i], (i == ntasks - 1) ? '\n' : ',');
	}

	return 0;
}
