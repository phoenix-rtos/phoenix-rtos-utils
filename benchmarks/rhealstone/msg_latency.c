/*
 * Phoenix-RTOS
 *
 * Rhealstone benchmark
 *
 * Inter-process message latency
 *
 * Copyright 2025 Phoenix Systems
 * Author: Lukasz Leczkowski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/msg.h>
#include <sys/threads.h>
#include <sys/wait.h>

#include "bench_common.h"


#define BENCHMARKS 10000

#define SENDER_NAME "rh_msg_latency_send"


int main(int argc, char *argv[])
{
	uint32_t port;
	uint64_t loopOverhead, benchStart, benchEnd;
	msg_t msg;
	msg_rid_t rid;
	pid_t pid;
	int status;
	char portStr[12];

	puts("Rhealstone benchmark suite:\nMessage Latency");

	if (bench_plat_initTimer() < 0) {
		puts("Platform timer init fail");
		return 1;
	}

	priority(1);

	if (portCreate(&port) < 0) {
		puts("portCreate fail");
		return 1;
	}

	loopOverhead = bench_plat_getTime();

	for (int i = 0; i < BENCHMARKS; i++) {
		__asm__ volatile("nop");
	}

	for (int i = 0; i < BENCHMARKS; i++) {
		__asm__ volatile("nop");
	}

	loopOverhead = bench_plat_getTime() - loopOverhead;

	snprintf(portStr, sizeof(portStr), "%u", port);
	char *senderArgv[] = { SENDER_NAME, portStr, NULL };

	pid = spawnSyspage(NULL, NULL, SENDER_NAME, senderArgv);
	if (pid < 0) {
		puts("spawnSyspage fail");
		portDestroy(port);
		return 1;
	}

	/* Receiver (high priority) */
	priority(2);

	/* Read start timestamp from first message (set by sender just before first msgSend) */
	msgRecv(port, &msg, &rid);
	benchStart = *(uint64_t *)msg.i.raw;
	msgRespond(port, &msg, rid);

	for (int i = 1; i < BENCHMARKS; i++) {
		msgRecv(port, &msg, &rid);
		msgRespond(port, &msg, rid);
	}

	benchEnd = bench_plat_getTime();

	waitpid(pid, &status, 0);

	bench_printResult(benchStart, benchEnd, BENCHMARKS, loopOverhead, 0);

	portDestroy(port);

	return 0;
}
