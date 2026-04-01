/*
 * Phoenix-RTOS
 *
 * Rhealstone benchmark
 *
 * Platform-dependent functions (imx6ull platform)
 *
 * Copyright 2026 Phoenix Systems
 * Author: Adam Debek
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <stdio.h>
#include <errno.h>
#include <sys/interrupt.h>
#include <sys/mman.h>
#include <sys/platform.h>

#include <phoenix/arch/armv7a/imx6ull/imx6ull.h>
#include <board_config.h>

#include "bench_plat.h"

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


uint32_t bench_plat_getOneTickAvg(void)
{
	return 8735;
}


uint32_t bench_plat_getOneTick(void)
{
	return 9200;
}
