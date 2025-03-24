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
#include <sys/interrupt.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/threads.h>

#include <board_config.h>
#include "bench_common.h"


#ifdef __CPU_GR740
#define IRQ_UNUSED 13
#else
#error "TODO: specify interrupt number"
#endif


#define BENCHMARKS 1000


static struct {
	volatile uint64_t benchEnd;
	uint64_t results[BENCHMARKS];
} common;


static int irqHandler(unsigned int n, void *arg)
{
	common.benchEnd = bench_getTime();

	return 0;
}


int main(int argc, char *argv[])
{
	puts("Rhealstone benchmark suite:\nInterrupt latency");

	volatile uint32_t *irqCtrl = mmap(NULL, _PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_DEVICE | MAP_PHYSMEM | MAP_ANONYMOUS, -1, (uintptr_t)INT_CTRL_BASE);

	priority(1);

	interrupt(IRQ_UNUSED, irqHandler, NULL, 0, NULL);

	for (int i = 0; i < BENCHMARKS; ++i) {
		common.benchEnd = 0;
		uint64_t benchStart = bench_getTime();

#ifdef __CPU_GR740
		/* Force interrupt on CPU 0 */
		*(irqCtrl + 2) = (1 << IRQ_UNUSED);
#else
		/* TODO: handle other platform - force interrupt */
#endif

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
