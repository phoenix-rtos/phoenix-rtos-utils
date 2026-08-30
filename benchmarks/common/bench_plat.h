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


/* Initialize timer. Returns < 0 on failure. */
int bench_plat_initTimer(void);


/* Get current timestamp from timer. */
uint64_t bench_plat_getTime(void);


/* Initialize a software-generated interrupt and set `irqHandler` as its handler. Returns < 0 on failure. */
int bench_plat_initIRQ(int (*irqHandler)(unsigned int, void *));


/* Trigger the software-generated interrupt. */
void bench_plat_triggerIRQ(void);


/* The number of delay loop iterations in `preemption.c` that makes the loop last
 * about one scheduling tick on average (1 ms in typical Phoenix-RTOS configuration).
 * The value should be calibrated per-platform, a suitable function for it is in `preemption.c`.
 */
uint32_t bench_plat_getOneTickAvg(void);


/* The number of delay loop iterations in `preemption.c` that makes the loop last
 * longer than one scheduling tick. Should be about 4% larger than bench_plat_getOneTickAvg().
 */
uint32_t bench_plat_getOneTick(void);


#endif /* _BENCH_PLAT_H_ */
