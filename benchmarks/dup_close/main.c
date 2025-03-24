/*
 * Phoenix-RTOS
 *
 * Dup/close benchmark
 *
 * Copyright 2025 Phoenix Systems
 * Author: Lukasz Leczkowski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <stdio.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/threads.h>
#include <sys/file.h>
#include <sys/time.h>
#include <fcntl.h>

#include "bench_common.h"

#define MAX_THREADS            100
#define THREAD_STACK_SIZE      4096
#define BENCHMARK_DURATION_SEC 15ULL


typedef struct {
	int thread_id;
	int fd;
} thread_arg;


static struct {
	atomic_int taskStart;
	unsigned int thread_ops_count[MAX_THREADS];
} common = {
	.taskStart = 0,
};


static void benchmark_thread(void *arg)
{
	thread_arg *a = (thread_arg *)arg;
	int fd = a->fd;
	int dup_fd;

	while (!common.taskStart) {
		usleep(0);
	}

	uint64_t start_cycles = bench_getTime();
	uint64_t end_cycles = start_cycles + (BENCHMARK_DURATION_SEC * 250000000ULL);  // 250 MHz * 1 second

	while (bench_getTime() < end_cycles) {
		dup_fd = dup(fd);
		if (dup_fd < 0) {
			perror("dup");
			endthread();
		}
		close(dup_fd);
		common.thread_ops_count[a->thread_id]++;
	}

	endthread();
}


int main(int argc, char *argv[])
{
	static uint8_t stacks[MAX_THREADS][THREAD_STACK_SIZE] __attribute__((aligned(8)));
	static handle_t threads[MAX_THREADS];
	static thread_arg thread_args[MAX_THREADS];

	priority(0);

	int nthreads = MAX_THREADS;

	if (argc > 1) {
		nthreads = atoi(argv[1]);
		if ((nthreads <= 0) || (nthreads > MAX_THREADS)) {
			nthreads = MAX_THREADS;
			printf("Number of threads limited to %d\n", MAX_THREADS);
		}
	}

	printf("Starting benchmark with %d threads for %llu seconds\n", nthreads, BENCHMARK_DURATION_SEC);
	int fd = open("/dev/console", O_RDWR);
	if (fd < 0) {
		perror("open");
		return -1;
	}

	for (int i = 0; i < nthreads; i++) {
		thread_args[i].thread_id = i;
		thread_args[i].fd = fd;

		if (beginthreadex(benchmark_thread, 2, stacks[i], THREAD_STACK_SIZE, &thread_args[i], &threads[i]) < 0) {
			puts("beginthreadex fail");
			close(fd);
			return -1;
		}
	}

	priority(4);
	common.taskStart = 1;

	for (int i = 0; i < nthreads; i++) {
		threadJoin(threads[i], 0);
	}
	close(fd);

	uint64_t ops_count = 0;
	for (int i = 0; i < nthreads; i++) {
		ops_count += common.thread_ops_count[i];
		printf("Thread %d operations: %u\n", i, common.thread_ops_count[i]);
	}

	printf("Benchmark completed.\nOperations per second: %llu\n", ops_count / BENCHMARK_DURATION_SEC);

	return 0;
}
