/*
 * Phoenix-RTOS
 *
 * Benchmarks
 *
 * Platform-dependent function definitions
 *
 * Copyright 2025 Phoenix Systems
 * Author: Jacek Maksymowicz
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */


#ifndef _BENCH_PLAT_H_
#define _BENCH_PLAT_H_

#include <inttypes.h>

#if defined(__CPU_GR740)
#define ONE_TICK     26000 /* Number dependent on CPU. For loop to this count must be longer than sleep period (1 ms) */
#define ONE_TICK_AVG 24990 /* For loop up to this count takes 1ms */
#else
#error "Unsupported platform"
#endif

/* Initialize timer. Returns < 0 on failure. */
int bench_plat_initTimer(void);


/* Get current timestamp from timer. */
uint64_t bench_plat_getTime(void);


/* Initialize a software-generated interrupt and set `irqHandler` as its handler. Returns < 0 on failure. */
int bench_plat_initIRQ(int (*irqHandler)(unsigned int, void *));


/* Trigger the software-generated interrupt. */
void bench_plat_triggerIRQ(void);


#endif /* _BENCH_PLAT_H_ */
