/*
 * Phoenix-RTOS
 *
 * Rhealstone benchmark
 *
 * Interrupt latency
 *
 * Copyright 2025 Phoenix Systems
 * Author: Lukasz Leczkowski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/threads.h>

#include "bench_common.h"


#define BENCHMARKS 1000


static struct {
	volatile uint64_t benchEnd;
	uint64_t results[BENCHMARKS];
} common;


static int irqHandler(unsigned int n, void *arg)
{
	common.benchEnd = bench_plat_getTime();

	return 0;
}


int main(int argc, char *argv[])
{
	puts("Rhealstone benchmark suite:\nInterrupt latency");

	if (bench_plat_initTimer() < 0) {
		puts("Platform timer init fail");
		return 1;
	}

	priority(1);

	if (bench_plat_initIRQ(irqHandler) < 0) {
		puts("IRQ init failed");
		return 1;
	}

	for (int i = 0; i < BENCHMARKS; ++i) {
		common.benchEnd = 0;
		uint64_t benchStart = bench_plat_getTime();

		bench_plat_triggerIRQ();

		while (common.benchEnd == 0) {
			__asm__ volatile("nop");
		}

		common.results[i] = common.benchEnd - benchStart;
	}

	uint64_t avg = 0;
	for (int i = 0; i < BENCHMARKS; ++i) {
		avg += common.results[i];
	}

	avg /= BENCHMARKS;

	printf("Average interrupt latency: %" PRIu64 "\n", avg);

	return 0;
}
