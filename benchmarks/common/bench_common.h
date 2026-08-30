/*
 * Phoenix-RTOS
 *
 * Benchmarks
 *
 * Common functions
 *
 * Copyright 2025 Phoenix Systems
 * Author: Lukasz Leczkowski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _COMMON_H_
#define _COMMON_H_


#include <stdint.h>
#include <sys/threads.h>

#include "bench_plat.h"


uint64_t bench_printResult(uint64_t start, uint64_t end, int loops, uint64_t loopOverhead, uint64_t singleOverhead);


uint64_t bench_plat_getTime(void);


uint64_t bench_mutexLockOverhead(handle_t mutex);


#endif
