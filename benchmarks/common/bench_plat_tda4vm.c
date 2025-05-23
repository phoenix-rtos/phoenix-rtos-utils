/*
 * Phoenix-RTOS
 *
 * Rhealstone benchmark
 *
 * Platform-dependent functions (TDA4VM platform)
 *
 * Copyright 2025 Phoenix Systems
 * Author: Jacek Maksymowicz
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include "bench_plat.h"

#include <stdio.h>
#include <sys/interrupt.h>
#include <sys/mman.h>
#include <sys/platform.h>
#include <errno.h>

#include <phoenix/arch/armv7r/tda4vm/tda4vm.h>

#include <board_config.h>


#define VIM_BASE_ADDRESS          0x40F80000
#define MCU_R5FSS0_CORE1_VALIRQ_0 56 /* This IRQ seems to be unused, so we can repurpose it for this test */

enum {
	vim_raw_m = (0x400 / 4), /* Raw status/set register */
};


static volatile uint32_t *irqCtrl_vim;
static uint32_t epoch;


static inline uint32_t getCycleCnt(void)
{
	uint32_t cycleCnt;
	asm volatile("mrc p15, 0, %0, c9, c13, 0" : "=r"(cycleCnt));
	return cycleCnt;
}


static inline uint32_t getPMOVSR(void)
{
	uint32_t pmovsr;
	asm volatile("mrc p15, 0, %0, c9, c12, 3" : "=r"(pmovsr));
	return pmovsr;
}


static inline void setPMOVSR(uint32_t newValue)
{
	asm volatile("mcr p15, 0, %0, c9, c12, 3" ::"r"(newValue));
}


int bench_plat_initTimer(void)
{
	platformctl_t pctl;
	pctl.action = pctl_set;
	pctl.type = pctl_cpuperfmon;
	pctl.cpuperfmon.user_access = 1;
	pctl.cpuperfmon.div64 = 1;
	pctl.cpuperfmon.reset_counter = 1;
	if (platformctl(&pctl) < 0) {
		return -1;
	}

	epoch = 0;
	setPMOVSR(1 << 31); /* Clear overflow */
	return 0;
}


/* In this implementation the timer overflows every 64 seconds, so if any test takes longer
 * the function needs to be called regularly */
uint64_t bench_plat_getTime(void)
{
	uint32_t cycleCnt = getCycleCnt();
	/* Check for counter overflow */
	if ((getPMOVSR() & (1 << 31)) != 0) {
		epoch += 1;
		setPMOVSR(1 << 31); /* Clear overflow */
		cycleCnt = getCycleCnt();
	}

	return (((uint64_t)epoch << 32) | cycleCnt) * 64;
}


int bench_plat_initIRQ(int (*irqHandler)(unsigned int, void *))
{
	void *mmapResult = mmap(NULL, _PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_DEVICE | MAP_PHYSMEM | MAP_ANONYMOUS, -1, (uintptr_t)VIM_BASE_ADDRESS);
	if (mmapResult == MAP_FAILED) {
		return -ENOMEM;
	}

	irqCtrl_vim = mmapResult;
	return interrupt(MCU_R5FSS0_CORE1_VALIRQ_0, irqHandler, NULL, 0, NULL);
}


static inline void vim_triggerIRQ(unsigned int irqn)
{
	unsigned int irq_reg = (irqn / 32) * 8;
	unsigned int irq_offs = irqn % 32;
	*(irqCtrl_vim + vim_raw_m + irq_reg) = 1u << irq_offs;
	asm volatile("dmb");
}


void bench_plat_triggerIRQ(void)
{
	vim_triggerIRQ(MCU_R5FSS0_CORE1_VALIRQ_0);
}


uint32_t bench_plat_getOneTickAvg(void)
{
	return 23810;
}


uint32_t bench_plat_getOneTick(void)
{
	return 24750;
}
