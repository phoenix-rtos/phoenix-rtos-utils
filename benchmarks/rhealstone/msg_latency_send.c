/*
 * Phoenix-RTOS
 *
 * Rhealstone benchmark
 *
 * Inter-process message latency - sender process
 *
 * Copyright 2025 Phoenix Systems
 * Author: Damian Loewnau
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <stdlib.h>
#include <string.h>
#include <sys/msg.h>
#include <sys/threads.h>

#include "bench_common.h"


#define BENCHMARKS 10000


int main(int argc, char *argv[])
{
	uint32_t port;
	msg_t msg;

	if (argc < 2) {
		return 1;
	}

	port = strtoul(argv[1], NULL, 10);

	if (bench_plat_initTimer() < 0) {
		return 1;
	}

	priority(3);

	memset(&msg, 0, sizeof(msg));
	msg.type = mtDevCtl;

	/* Embed start timestamp in first message so receiver gets accurate t0 */
	uint64_t *ts = (uint64_t *)msg.i.raw;
	*ts = bench_plat_getTime();
	msgSend(port, &msg);

	*ts = 0;
	for (int i = 1; i < BENCHMARKS; i++) {
		msgSend(port, &msg);
	}

	return 0;
}
