/*
 * Phoenix-RTOS
 *
 * Rhealstone benchmark
 *
 * Message latency
 *
 * Copyright 2025 Phoenix Systems
 * Author: Lukasz Leczkowski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/threads.h>

#include "bench_common.h"


#define BENCHMARKS   100000
#define MESSAGE_SIZE (sizeof(long) * 4)


struct queueCtx {
	handle_t mutex;
	handle_t condReady;
	handle_t condFree;
	int head;
	int tail;
	int currSize;
	int totalSize;
	size_t itemSize;
	void *buffer;
};


static struct {
	struct queueCtx queue;
	uint64_t benchStart, benchEnd;
	unsigned char stack[2][4096] __attribute__((aligned(8)));
} common;


static int queueCreate(struct queueCtx *ctx, int totalCount, size_t itemSize)
{
	ctx->buffer = malloc(totalCount * itemSize);
	if (ctx->buffer == NULL) {
		return -1;
	}
	ctx->head = 0;
	ctx->tail = -1;
	ctx->currSize = 0;
	ctx->totalSize = totalCount;
	ctx->itemSize = itemSize;
	if (mutexCreate(&ctx->mutex) < 0) {
		free(ctx->buffer);
		return -1;
	}
	if (condCreate(&ctx->condReady) < 0) {
		resourceDestroy(ctx->mutex);
		free(ctx->buffer);
		return -1;
	}
	if (condCreate(&ctx->condFree) < 0) {
		resourceDestroy(ctx->condReady);
		resourceDestroy(ctx->mutex);
		free(ctx->buffer);
		return -1;
	}

	return 0;
}


static void queueDestroy(struct queueCtx *ctx)
{
	free(ctx->buffer);
	ctx->buffer = NULL;
	resourceDestroy(ctx->mutex);
	resourceDestroy(ctx->condReady);
	resourceDestroy(ctx->condFree);
}


static void queueSend(struct queueCtx *ctx, void *buf)
{
	mutexLock(ctx->mutex);
	while (ctx->currSize == ctx->totalSize) {
		condWait(ctx->condFree, ctx->mutex, 0);
	}
	ctx->tail = (ctx->tail + 1) % ctx->totalSize;
	memcpy((char *)ctx->buffer + ctx->tail * ctx->itemSize, buf, ctx->itemSize);
	ctx->currSize++;
	mutexUnlock(ctx->mutex);
	condSignal(ctx->condReady);
}


static void queueRecv(struct queueCtx *ctx, void *buf)
{
	mutexLock(ctx->mutex);
	while (ctx->currSize == 0) {
		condWait(ctx->condReady, ctx->mutex, 0);
	}
	memcpy(buf, (char *)ctx->buffer + ctx->head * ctx->itemSize, ctx->itemSize);
	ctx->head = (ctx->head + 1) % ctx->totalSize;
	ctx->currSize--;
	condSignal(ctx->condFree);
	mutexUnlock(ctx->mutex);
}


/* Low priority */
static void task1(void *arg)
{
	long sendBuf[4] = { 0 };

	common.benchStart = bench_plat_getTime();

	for (int i = 0; i < BENCHMARKS; i++) {
		queueSend(&common.queue, sendBuf);
	}
	endthread();
}

/* High priority */
static void task2(void *arg)
{
	long recvBuf[4];

	for (int i = 0; i < BENCHMARKS; i++) {
		queueRecv(&common.queue, recvBuf);
	}
	common.benchEnd = bench_plat_getTime();
	endthread();
}


int main(int argc, char *argv[])
{
	puts("Rhealstone benchmark suite:\nMessage Latency");

	if (bench_plat_initTimer() < 0) {
		puts("Platform timer init fail");
		return 1;
	}

	priority(1);

	if (queueCreate(&common.queue, 1, MESSAGE_SIZE) < 0) {
		puts("queueCreate fail");
		return -1;
	}

	uint64_t loopOverhead = bench_plat_getTime();

	for (int i = 0; i < BENCHMARKS; i++) {
		__asm__ volatile("nop");
	}

	for (int i = 0; i < BENCHMARKS; i++) {
		__asm__ volatile("nop");
	}

	loopOverhead = bench_plat_getTime() - loopOverhead;

	int tid1, tid2;
	if (beginthreadex(task2, 2, common.stack[1], sizeof(common.stack[1]), NULL, &tid2) < 0) {
		puts("beginthreadex fail");
		return -1;
	}

	if (beginthreadex(task1, 3, common.stack[0], sizeof(common.stack[0]), NULL, &tid1) < 0) {
		puts("beginthreadex fail");
		return -1;
	}

	priority(4);
	usleep(0);

	threadJoin(tid1, 0);
	threadJoin(tid2, 0);

	bench_printResult(common.benchStart, common.benchEnd, BENCHMARKS, loopOverhead, 0);

	queueDestroy(&common.queue);

	return 0;
}
