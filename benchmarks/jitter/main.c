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


#define THREAD_STACK_SIZE 1024
#define JITTER_SAMPLES    5000
#define MAX_TASKS         1024
#define MAX_BG_TASKS      256
#define MAX_JITTER_TASKS  4


static struct {
	unsigned int idleCounters[MAX_BG_TASKS];

	uint64_t jitter[MAX_JITTER_TASKS][JITTER_SAMPLES];
	handle_t jitterCond[MAX_JITTER_TASKS];
	handle_t jitterMutex[MAX_JITTER_TASKS];

	atomic_bool taskStart;
	volatile int taskEnd;

	uint8_t stack[MAX_TASKS][THREAD_STACK_SIZE] __attribute__((aligned(8)));
	uint8_t bgstack[MAX_BG_TASKS][THREAD_STACK_SIZE] __attribute__((aligned(8)));
} common = {
	.taskStart = false,
};


void jitterTask(void *arg)
{
	unsigned int n = (unsigned int)arg;
	time_t sleep;

	switch (n) {
		case 0:
			sleep = 1000;
			break;
		case 1:
			sleep = 1400;
			break;
		case 2:
			sleep = 1800;
			break;
		case 3:
			sleep = 2000;
			break;
		default:
			endthread();
			break;
	}

	mutexLock(common.jitterMutex[n]);

	while (!common.taskStart) {
		usleep(0);
	}

	time_t now;
	gettime(&now, NULL);
	for (int i = 0; (i < JITTER_SAMPLES) && !common.taskEnd; i++) {
		now += sleep;
		uint64_t start = bench_getTime();
		/* condWait - we're able to use absolute timeout */
		condWait(common.jitterCond[n], common.jitterMutex[n], now);
		uint64_t end = bench_getTime();

		common.jitter[n][i] = end - start;
	}

	mutexUnlock(common.jitterMutex[n]);

	endthread();
}


void idleTask(void *arg)
{
	unsigned int n = (unsigned int)arg;
	while (!common.taskStart) {
		usleep(0);
	}

	while (!common.taskEnd) {
		++common.idleCounters[n];
	}

	endthread();
}


static int doTest(void (*task)(void *), int ntasks, void (*bgTask)(void *), int nbgtasks, int sleepTimeSec)
{
	static int tid[MAX_TASKS];
	static int bgTid[MAX_BG_TASKS];

	common.taskStart = false;
	common.taskEnd = false;

	if (ntasks > MAX_JITTER_TASKS) {
		ntasks = MAX_JITTER_TASKS;
	}

	for (int i = 0; i < ntasks; i++) {
		if (beginthreadex(task, 2, common.stack[i], sizeof(common.stack[i]), (void *)i, &tid[i]) < 0) {
			puts("beginthreadex fail");
			return -1;
		}
	}

	if (nbgtasks > MAX_BG_TASKS) {
		nbgtasks = MAX_BG_TASKS;
	}

	for (int i = 0; i < nbgtasks; i++) {
		if (beginthreadex(bgTask, 3, common.bgstack[i], sizeof(common.bgstack[i]), (void *)i, &bgTid[i]) < 0) {
			puts("beginthreadex fail");
			return -1;
		}
	}

	common.taskStart = true;

	usleep(sleepTimeSec * 1000 * 1000);

	common.taskEnd = true;

	for (int i = 0; i < ntasks; i++) {
		threadJoin(tid[i], 0);
	}

	for (int i = 0; i < nbgtasks; i++) {
		threadJoin(bgTid[i], 0);
	}

	return 0;
}


int main(int argc, char *argv[])
{
	int scenario;
	puts("Starting benchmark");

	if (argc > 1) {
		scenario = atoi(argv[1]);
		if ((scenario < 1) || (scenario > 5)) {
			puts("Invalid scenario");
			exit(EXIT_FAILURE);
		}
	}
	else {
		puts("Choose scenario (1 - 5)");
		exit(EXIT_FAILURE);
	}

	for (int i = 0; i < MAX_JITTER_TASKS; i++) {
		struct condAttr attr = { .clock = PH_CLOCK_MONOTONIC };
		if (condCreateWithAttr(&common.jitterCond[i], &attr) != 0) {
			puts("condCreate fail");
			exit(EXIT_FAILURE);
		}
		if (mutexCreate(&common.jitterMutex[i]) != 0) {
			puts("mutexCreate fail");
			exit(EXIT_FAILURE);
		}
	}

	if (priority(0) < 0) {
		puts("priority fail");
		exit(EXIT_FAILURE);
	}

	/* TODO: specify parameters through cmdline (?) */
	int ntasks = 0, nbgtasks = 0, sleeptime = 0;
	switch (scenario) {
		case 1:
			ntasks = 1;
			nbgtasks = 0;
			sleeptime = 10;
			break;

		case 2:
			ntasks = 1;
			nbgtasks = 10;
			sleeptime = 10;
			break;

		case 3:
			ntasks = 1;
			nbgtasks = 256;
			sleeptime = 10;
			break;

		case 4:
			ntasks = 2;
			nbgtasks = 256;
			sleeptime = 15;
			break;


		case 5:
			ntasks = 4;
			nbgtasks = 256;
			sleeptime = 20;
			break;


		default:
			break;
	}

	if (doTest(jitterTask, ntasks, idleTask, nbgtasks, sleeptime) < 0) {
		return -1;
	}

	printf("Jitter benchmark results (%d tasks, %d background tasks):\n", ntasks, nbgtasks);

	for (int i = 0; i < ntasks; i++) {
		printf("Jitter task %d:\n", i);
		for (int j = 0; j < JITTER_SAMPLES; j++) {
			printf("%" PRIu64 "%c", common.jitter[i][j], (j == JITTER_SAMPLES - 1) ? '\n' : ',');
		}
	}

	for (int i = 0; i < MAX_JITTER_TASKS; i++) {
		resourceDestroy(common.jitterCond[i]);
		resourceDestroy(common.jitterMutex[i]);
	}

	return 0;
}
