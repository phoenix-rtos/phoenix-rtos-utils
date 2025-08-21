#include "kernel_comm.h"

#include "hal.h"

#include <string.h>
#include <sys/msg.h>
#include <errno.h>
#include <stdio.h>

#define PRINT_ERR(args...) fprintf(stderr, "Coredump server: " args)

int coredump_waitForCrash(coredump_general *crashInfo)
{
	msg_t msg;

	msg.oid.id = 0;
	msg.oid.port = 1;
	msg.type = mtOpen;
	msg.i.size = 0;
	msg.i.data = NULL;
	msg.o.size = sizeof(*crashInfo);
	msg.o.data = crashInfo;

	int ret = msgSend(1, &msg);
	if (ret != 0) {
		PRINT_ERR("msgSend open failed with %d: %s\n", ret, strerror(ret));
		return ret;
	}
	if (msg.o.err != 0) {
		PRINT_ERR("open failed with %d: %s\n", msg.o.err, strerror(msg.o.err));
		return msg.o.err;
	}

	return 0;
}


int coredump_getThreadContext(int tid, coredump_thread *resp)
{
	msg_t msg;

	coredump_req req = {
		.type = COREDUMP_REQ_THREAD,
		.thread = {
			.tid = tid,
		},
	};

	msg.oid.id = 0;
	msg.oid.port = 1;
	msg.type = mtRead;
	msg.i.size = sizeof(req);
	msg.i.data = &req;
	msg.o.size = sizeof(coredump_thread) + sizeof(cpu_context_t);
	msg.o.data = resp;

	int ret = msgSend(1, &msg);
	if (ret != 0) {
		PRINT_ERR("msgSend req_thread failed with %d: %s\n", ret, strerror(ret));
		return ret;
	}
	if (msg.o.err != 0) {
		PRINT_ERR("read req_thread failed with %d: %s\n", msg.o.err, strerror(msg.o.err));
		return msg.o.err;
	}

	return 0;
}


int coredump_getMemList(size_t bufSize, coredump_memseg *resp)
{
	msg_t msg;
	coredump_req req = {
		.type = COREDUMP_REQ_MEMLIST,
	};

	msg.oid.id = 0;
	msg.oid.port = 1;
	msg.type = mtRead;
	msg.i.size = sizeof(req);
	msg.i.data = &req;
	msg.o.size = bufSize;
	msg.o.data = resp;

	int ret = msgSend(1, &msg);
	if (ret != 0) {
		PRINT_ERR("msgSend req_memlist failed with %d: %s\n", ret, strerror(ret));
		return ret;
	}
	if (msg.o.err != 0) {
		PRINT_ERR("read req_memlist failed with %d: %s\n", msg.o.err, strerror(msg.o.err));
		return msg.o.err;
	}
	return 0;
}


int coredump_getRelocs(size_t bufSize, coredump_reloc *resp)
{
#ifdef NOMMU
	msg_t msg;
	coredump_req req = {
		.type = COREDUMP_REQ_RELOC,
	};

	msg.oid.id = 0;
	msg.oid.port = 1;
	msg.type = mtRead;
	msg.i.size = sizeof(req);
	msg.i.data = &req;
	msg.o.size = bufSize;
	msg.o.data = resp;

	int ret = msgSend(1, &msg);
	if (ret != 0) {
		PRINT_ERR("msgSend req_reloc failed with %d: %s\n", ret, strerror(ret));
		return ret;
	}
	if (msg.o.err != 0) {
		PRINT_ERR("read req_reloc failed with %d: %s\n", msg.o.err, strerror(msg.o.err));
		return msg.o.err;
	}

#endif

	return 0;
}


int coredump_getMemory(void *startAddr, size_t len, msg_t *msg, msg_rid_t *rid)
{
	coredump_req req = {
		.type = COREDUMP_REQ_MEM,
		.mem = {
			.startAddr = startAddr,
			.size = len,
			.responsePort = msg->oid.port,
		},
	};

	msg->type = mtRead;
	msg->i.size = sizeof(req);
	msg->i.data = &req;
	msg->o.size = 0;

	int ret = msgSend(1, msg);
	if (ret != 0) {
		PRINT_ERR("msgSend req_mem failed with %d: %s\n", ret, strerror(ret));
		return ret;
	}
	if (msg->o.err != 0) {
		PRINT_ERR("read req_mem failed with %d: %s\n", msg->o.err, strerror(msg->o.err));
		PRINT_ERR("%p %zu\n", startAddr, len);
		return msg->o.err;
	}

	ret = msgRecv(msg->oid.port, msg, rid);
	if (ret < 0) {
		PRINT_ERR("getMemory msgRecv failed with %d: %s\n", ret, strerror(ret));
		return ret;
	}
	if (msg->type != mtWrite) {
		PRINT_ERR("getMemory msg type %d != mtWrite\n", msg->type);
		msg->o.err = EINVAL;
		msgRespond(msg->oid.port, msg, *rid);
		return -EINVAL;
	}
	return 0;
}


void coredump_putMemory(msg_t *msg, msg_rid_t rid)
{
	msg->o.err = EOK;
	msgRespond(msg->oid.port, msg, rid);
}


int coredump_closeCrash(void)
{
	msg_t msg;

	msg.oid.id = 0;
	msg.oid.port = 1;
	msg.type = mtClose;
	msg.i.size = 0;
	msg.i.data = NULL;
	msg.o.size = 0;
	msg.o.data = NULL;

	int ret = msgSend(1, &msg);
	if (ret != 0) {
		PRINT_ERR("msgSend close failed with %d: %s\n", ret, strerror(ret));
		return ret;
	}
	if (msg.o.err != 0) {
		PRINT_ERR("close failed with %d: %s\n", msg.o.err, strerror(msg.o.err));
		return msg.o.err;
	}

	return 0;
}
