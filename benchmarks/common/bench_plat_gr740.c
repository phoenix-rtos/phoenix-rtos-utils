/*
 * Phoenix-RTOS
 *
 * Rhealstone benchmark
 *
 * Platform-dependent functions (GR740 platform)
 *
 * Copyright 2025 Phoenix Systems
 * Author: Lukasz Leczkowski, Jacek Maksymowicz
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include "bench_plat.h"

#include <stdio.h>
#include <sys/interrupt.h>
#include <sys/mman.h>
#include <errno.h>

#include <board_config.h>

#define IRQ_UNUSED 13

static volatile uint32_t *irqCtrl;


int bench_plat_initTimer(void)
{
	/* No config necessary on this platform */
	return 0;
}


uint64_t bench_plat_getTime(void)
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


int bench_plat_initIRQ(int (*irqHandler)(unsigned int, void *))
{
	void *mmapResult = mmap(NULL, _PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_DEVICE | MAP_PHYSMEM | MAP_ANONYMOUS, -1, (uintptr_t)INT_CTRL_BASE);
	if (mmapResult == MAP_FAILED) {
		return -ENOMEM;
	}

	irqCtrl = mmapResult;
	return interrupt(IRQ_UNUSED, irqHandler, NULL, 0, NULL);
}


void bench_plat_triggerIRQ(void)
{
	*(irqCtrl + 2) = (1 << IRQ_UNUSED);
}


uint32_t bench_plat_getOneTickAvg(void)
{
	return 24990;
}


uint32_t bench_plat_getOneTick(void)
{
	return 26000;
}
