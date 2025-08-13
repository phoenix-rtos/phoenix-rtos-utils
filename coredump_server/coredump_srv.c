/*
 * Phoenix-RTOS
 *
 * Operating system kernel
 *
 * Process coredump
 *
 * Copyright 2025 Phoenix Systems
 * Author: Jakub Klimek
 *
 * %LICENSE%
 */

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/msg.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stddef.h>
#include <phoenix/types.h>
#include <posix/utils.h>
#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>

#include "encoding.h"
#include "elf.h"
#include "hal.h"
#include "settings.h"
#include "kernel_comm.h"


#define PRINT_ERR(args...) fprintf(stderr, "Coredump server: " args)

#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))

#define COREDUMP_OUTBUF_SIZE 128

#define COREDUMP_START "\n_____________COREDUMP_START_____________\n"
#define COREDUMP_END   "\n______________COREDUMP_END______________\n"

#define PRSTATUS_NAME "CORE"

#define NT_LMA      0x00414D4C /* ASCII for "LMA" (load memory address) */
#define NT_LMA_NAME "PHOENIX"


const coredump_opts_t defaultOpts = {
	.maxThreads = 4,
	.maxStackSize = 0,
	.memScope = COREDUMP_MEM_ALL_STACKS,
	.fpContext = 0,
	.maxMemChunk = 0,
	.print = 1,
	.printSleep = 10000, /* 10ms */
	.savepath = "/coredumps",
	.maxFiles = 0,
};


struct {
	/* Crash Info */
	struct {
		void *addr;
		size_t size;
	} *threadStacks;
	size_t threadCnt;
	coredump_memseg *memList;
	size_t memSegCnt;
	coredump_reloc relocs[8];
	int elfclass32;

	/* File Output */
	FILE *fp;

	/* Text Output */
	char outBuf[COREDUMP_OUTBUF_SIZE];
	size_t outCur;

	__u8 rle_last;
	size_t rle_count;

	lib_base64_ctx b64;
	crc32_t crc32;

	/* Settings */
	pthread_mutex_t mutex;
	coredump_opts_t opts;

	/* Messaging */
	__u32 memRecvPort;
	oid_t settingsDev;
} state;


static void coredump_print(const char *data, size_t len)
{
	if (state.opts.print) {
		write(STDERR_FILENO, data, len);
		usleep(state.opts.printSleep);
	}
}


static void coredump_writeBuf(const char *data, size_t len)
{
	while (state.outCur + len >= sizeof(state.outBuf) - 1) {
		memcpy(state.outBuf + state.outCur, data, sizeof(state.outBuf) - 1 - state.outCur);
		state.outBuf[sizeof(state.outBuf) - 1] = '\0';
		coredump_print(state.outBuf, sizeof(state.outBuf));
		data += sizeof(state.outBuf) - 1 - state.outCur;
		len -= sizeof(state.outBuf) - 1 - state.outCur;
		state.outCur = 0;
	}
	memcpy(state.outBuf + state.outCur, data, len);
	state.outCur += len;
	if (state.outCur >= sizeof(state.outBuf) - 1) {
		state.outBuf[sizeof(state.outBuf) - 1] = '\0';
		coredump_print(state.outBuf, sizeof(state.outBuf));
		state.outCur = 0;
	}
}


static void coredump_encodeByte(const __u8 byte)
{
	int n = enc_base64EncodeByte(&state.b64, byte);
	coredump_writeBuf(state.b64.outBuf, n);
}


static void coredump_encodeRleLength(void)
{
	__u8 byte;
	while (state.rle_count > 0) {
		byte = state.rle_count & 0x7F;
		state.rle_count >>= 7;
		if (state.rle_count > 0) {
			byte |= 0x80;
		}
		coredump_encodeByte(byte);
	}
}


static void coredump_encodeChunk(const __u8 *buf, size_t len)
{
	size_t i;
	__u8 byte;
	for (i = 0; i < len; i++) {
		/* making sure that crc is coherent with dumped data even if the buf is written to by other process */
		byte = buf[i];

		if (state.fp != NULL) {
			fwrite(&byte, 1, 1, state.fp);
		}

		state.crc32 = enc_crc32NextByte(state.crc32, byte);

		if (state.rle_last == byte) {
			state.rle_count++;
			continue;
		}
		if ((state.rle_count > 3) || ((state.rle_last == 0xFE) && (state.rle_count > 0))) {
			coredump_encodeByte(0xFE);
			coredump_encodeRleLength();
			coredump_encodeByte(state.rle_last);
		}
		else {
			while (state.rle_count > 0) {
				coredump_encodeByte(state.rle_last);
				state.rle_count--;
			}
		}
		state.rle_count = 1;
		state.rle_last = byte;
	}
}


static int coredump_dumpMemory(void *startAddr, size_t len)
{
	msg_t msg;
	msg_rid_t rid;

	msg.oid.id = 0;
	msg.oid.port = state.memRecvPort;

	size_t memChunkSize = len;
	if (state.opts.maxMemChunk != 0) {
		memChunkSize = min(memChunkSize, state.opts.maxMemChunk);
	}

	void *curAddr = startAddr;

	while (curAddr < startAddr + len) {
		memChunkSize = min(memChunkSize, startAddr + len - curAddr);
		int ret = coredump_getMemory(curAddr, memChunkSize, &msg, &rid);
		if (ret != 0) {
			PRINT_ERR("coredump_getMemory failed with %d\n", ret);
			return ret;
		}
		coredump_encodeChunk(msg.i.data, memChunkSize);
		coredump_putMemory(&msg, rid);
		curAddr += memChunkSize;
	}

	return 0;
}


static void coredump_init(const char *path, int signal)
{
	state.outCur = 0;
	state.rle_last = -1;
	state.rle_count = 0;
	state.crc32 = LIB_CRC32_INIT;
	enc_base64Init(&state.b64);

	coredump_print(COREDUMP_START, sizeof(COREDUMP_START) - 1);
	coredump_print(path, strlen(path));
	coredump_print(": ", 2);
	coredump_print(strsignal(signal), strlen(strsignal(signal)));
	coredump_print(";\n", 2);
}


static void coredump_finalize(void)
{
	crc32_t crc = enc_crc32Finalize(state.crc32);
	coredump_encodeChunk((__u8 *)&crc, sizeof(crc));

	if ((state.rle_count > 3) || (state.rle_last == 0xFE)) {
		coredump_encodeByte(0xFE);
		coredump_encodeRleLength();
		coredump_encodeByte(state.rle_last);
	}
	else {
		while (state.rle_count > 0) {
			coredump_encodeByte(state.rle_last);
			state.rle_count--;
		}
	}

	int n = enc_base64Finalize(&state.b64);
	coredump_writeBuf(state.b64.outBuf, n);


	if (state.outCur > 0) {
		state.outBuf[state.outCur++] = '\0';
		coredump_print(state.outBuf, state.outCur);
	}

	coredump_print(COREDUMP_END, sizeof(COREDUMP_END) - 1);
}


static void coredump_dumpElfHeader32(size_t segCnt)
{
	Elf32_Ehdr hdr;

	memcpy(hdr.e_ident, ELFMAG, sizeof(ELFMAG));
	memset(hdr.e_ident + sizeof(ELFMAG), 0, sizeof(hdr.e_ident) - sizeof(ELFMAG));
	hdr.e_ident[EI_CLASS] = ELFCLASS32;
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	hdr.e_ident[EI_DATA] = ELFDATA2LSB;
#else
	hdr.e_ident[EI_DATA] = ELFDATA2MSB;
#endif
	hdr.e_ident[EI_VERSION] = 1; /* EV_CURRENT */
	hdr.e_ident[EI_OSABI] = ELFOSABI_SYSV;
	hdr.e_type = ET_CORE;
	hdr.e_machine = HAL_ELF_MACHINE;
	hdr.e_version = 1; /* EV_CURRENT */
	hdr.e_phoff = sizeof(Elf32_Ehdr);
	hdr.e_ehsize = sizeof(Elf32_Ehdr);
	hdr.e_phentsize = sizeof(Elf32_Phdr);
	hdr.e_phnum = 1 + segCnt;
	hdr.e_shoff = 0;
	hdr.e_flags = 0;
	hdr.e_shentsize = 0;
	hdr.e_shnum = 0;
	hdr.e_shstrndx = 0;
	hdr.e_entry = 0;

	coredump_encodeChunk((__u8 *)&hdr, sizeof(hdr));
}


static void coredump_dumpElfHeader64(size_t segCnt)
{
	Elf64_Ehdr hdr;

	memcpy(hdr.e_ident, ELFMAG, sizeof(ELFMAG));
	memset(hdr.e_ident + sizeof(ELFMAG), 0, sizeof(hdr.e_ident) - sizeof(ELFMAG));
	hdr.e_ident[EI_CLASS] = ELFCLASS64;
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	hdr.e_ident[EI_DATA] = ELFDATA2LSB;
#else
	hdr.e_ident[EI_DATA] = ELFDATA2MSB;
#endif
	hdr.e_ident[EI_VERSION] = 1; /* EV_CURRENT */
	hdr.e_ident[EI_OSABI] = ELFOSABI_SYSV;
	hdr.e_type = ET_CORE;
	hdr.e_machine = HAL_ELF_MACHINE;
	hdr.e_version = 1; /* EV_CURRENT */
	hdr.e_phoff = sizeof(Elf64_Ehdr);
	hdr.e_ehsize = sizeof(Elf64_Ehdr);
	hdr.e_phentsize = sizeof(Elf64_Phdr);
	hdr.e_phnum = 1 + segCnt;
	hdr.e_shoff = 0;
	hdr.e_flags = 0;
	hdr.e_shentsize = 0;
	hdr.e_shnum = 0;
	hdr.e_shstrndx = 0;
	hdr.e_entry = 0;

	coredump_encodeChunk((__u8 *)&hdr, sizeof(hdr));
}


static void coredump_dumpElfHeader(size_t segCnt)
{
	if (state.elfclass32) {
		coredump_dumpElfHeader32(segCnt);
	}
	else {
		coredump_dumpElfHeader64(segCnt);
	}
}


static size_t align4(size_t size)
{
	return (size + 3) & ~3;
}


static int coredump_dumpThreadNotes(coredump_thread *threadInfo)
{
	const __u32 zero = 0;

	Elf32_Nhdr nhdr; /* Elf64_Nhdr is identical to Elf32_Nhdr */
	elf_prstatus prstatus;
	elf_thread_aux threadAux;

	nhdr.n_namesz = sizeof(PRSTATUS_NAME);
	nhdr.n_descsz = sizeof(elf_prstatus);
	nhdr.n_type = NT_PRSTATUS;
	coredump_encodeChunk((__u8 *)&nhdr, sizeof(nhdr));
	coredump_encodeChunk((__u8 *)PRSTATUS_NAME, sizeof(PRSTATUS_NAME));
	/* alignment */
	coredump_encodeChunk((__u8 *)&zero, align4(sizeof(PRSTATUS_NAME)) - sizeof(PRSTATUS_NAME));

	memset(&prstatus, 0, sizeof(prstatus));
	int ret = hal_fillPrStatus((cpu_context_t *)threadInfo->context, &prstatus, state.memRecvPort, &state.opts);
	if (ret != 0) {
		PRINT_ERR("hal_fillPrStatus failed with %d\n", ret);
		return ret;
	}
	prstatus.pr_pid = threadInfo->tid;
	coredump_encodeChunk((__u8 *)&prstatus, sizeof(prstatus));

	hal_createThreadAuxNotes((cpu_context_t *)threadInfo->context, &threadAux, &state.opts);
	coredump_encodeChunk((__u8 *)&threadAux, hal_threadAuxNotesSize(&state.opts));
	return 0;
}


static int coredump_dumpAllThreadsNotes(int crashedTid)
{
	coredump_thread *threadInfo = malloc(sizeof(coredump_thread) + sizeof(cpu_context_t));
	if (threadInfo == NULL) {
		PRINT_ERR("malloc failed\n");
		return ENOMEM;
	}

	int nextTid = crashedTid;
	for (size_t i = 0; i < state.threadCnt; i++) {
		int ret = coredump_getThreadContext(nextTid, threadInfo);
		if (ret != 0) {
			PRINT_ERR("getThreadContext failed\n");
			free(threadInfo);
			return ret;
		}

		ret = coredump_dumpThreadNotes(threadInfo);
		if (ret != 0) {
			free(threadInfo);
			return ret;
		}

		nextTid = threadInfo->nextTid;
	}
	free(threadInfo);
	return 0;
}


static size_t coredump_LMANoteSize(void)
{

#ifdef NOMMU
	int cnt;

	for (cnt = 0; cnt < sizeof(state.relocs) / sizeof(state.relocs[0]); cnt++) {
		if (state.relocs[cnt].pbase == NULL) {
			break;
		}
	}
	if (cnt > 0) {
		return cnt * 2 * sizeof(Elf32_Addr) + sizeof(Elf32_Nhdr) + sizeof(NT_LMA_NAME);
	}

#endif /* NOMMU */

	return 0;
}


static void coredump_dumpLMANote(void)
{
#ifdef NOMMU
	int cnt = 0;
	for (cnt = 0; cnt < sizeof(state.relocs) / sizeof(state.relocs[0]); cnt++) {
		if (state.relocs[cnt].pbase == NULL) {
			break;
		}
	}

	Elf32_Nhdr nhdr = {
		.n_type = NT_LMA,
		.n_namesz = sizeof(NT_LMA_NAME),
		.n_descsz = cnt * 2 * sizeof(Elf32_Addr)
	};
	coredump_encodeChunk((__u8 *)&nhdr, sizeof(nhdr));
	coredump_encodeChunk((__u8 *)NT_LMA_NAME, sizeof(NT_LMA_NAME));

	coredump_encodeChunk((__u8 *)state.relocs, cnt * 2 * sizeof(Elf32_Addr));
#endif /* NOMMU */
}


static size_t coredump_findStack(void **currentSP, void *ustack)
{
	for (int i = 0; i < state.memSegCnt; i++) {
		if (state.memList[i].startAddr <= ustack &&
				state.memList[i].endAddr > ustack) {
			if ((*currentSP >= (void *)((char *)state.memList[i].endAddr)) || (*currentSP < state.memList[i].startAddr)) {
				*currentSP = state.memList[i].startAddr;
			}

			size_t stackSize = (char *)state.memList[i].endAddr - (char *)*currentSP;
			if (state.opts.maxStackSize > 0) {
				stackSize = min(stackSize, state.opts.maxStackSize);
			}
			return stackSize;
		}
	}
	return 0;
}


static int coredump_findAllStacks(int crashedTid)
{
	coredump_thread *threadInfo = malloc(sizeof(coredump_thread) + sizeof(cpu_context_t));
	if (threadInfo == NULL) {
		PRINT_ERR("malloc failed\n");
		return ENOMEM;
	}

	int nextTid = crashedTid;
	for (size_t i = 0; i < state.threadCnt; i++) {
		int ret = coredump_getThreadContext(nextTid, threadInfo);
		if (ret != 0) {
			PRINT_ERR("getThreadContext failed\n");
			free(threadInfo);
			return ret;
		}

		state.threadStacks[i].addr = hal_cpuGetUserSP((cpu_context_t *)threadInfo->context);
		state.threadStacks[i].size = coredump_findStack(&state.threadStacks[i].addr, threadInfo->stackAddr);

		nextTid = threadInfo->nextTid;
	}
	free(threadInfo);
	return 0;
}


static int coredump_dumpAllMem(void)
{
	int ret;
	for (size_t i = 0; i < state.memSegCnt; i++) {
		size_t len = (char *)state.memList[i].endAddr - (char *)state.memList[i].startAddr;
		ret = coredump_dumpMemory(state.memList[i].startAddr, len);
		if (ret != 0) {
			PRINT_ERR("Failed to dump memory segment %zu\n", i);
			return ret;
		}
	}
	return 0;
}


static void coredump_dumpPhdr32(__u32 type, size_t offset, void *vaddr, size_t size)
{
	Elf32_Phdr phdr;
	phdr.p_type = type;
	phdr.p_offset = offset;
	phdr.p_vaddr = (Elf32_Addr)(uintptr_t)vaddr;
	phdr.p_paddr = 0;
	phdr.p_filesz = size;
	phdr.p_memsz = size;
	phdr.p_flags = 0;
	phdr.p_align = 0;
	coredump_encodeChunk((__u8 *)&phdr, sizeof(phdr));
}


static void coredump_dumpPhdr64(__u32 type, size_t offset, void *vaddr, size_t size)
{
	Elf64_Phdr phdr;
	phdr.p_type = type;
	phdr.p_offset = offset;
	phdr.p_vaddr = (Elf64_Addr)(uintptr_t)vaddr;
	phdr.p_paddr = 0;
	phdr.p_filesz = size;
	phdr.p_memsz = size;
	phdr.p_flags = 0;
	phdr.p_align = 0;
	coredump_encodeChunk((__u8 *)&phdr, sizeof(phdr));
}


static void coredump_dumpPhdr(__u32 type, size_t offset, void *vaddr, size_t size)
{
	if (state.elfclass32) {
		coredump_dumpPhdr32(type, offset, vaddr, size);
	}
	else {
		coredump_dumpPhdr64(type, offset, vaddr, size);
	}
}


static void coredump_dumpAllPhdrs(size_t segCnt)
{
	const size_t THREAD_NOTES_SIZE = sizeof(Elf32_Nhdr) +
			((sizeof(PRSTATUS_NAME) + 3) & ~3) +
			sizeof(elf_prstatus) + hal_threadAuxNotesSize(&state.opts);
	const size_t NOTES_SIZE = hal_procAuxNotesSize(&state.opts) + state.threadCnt * (THREAD_NOTES_SIZE) + coredump_LMANoteSize();
	size_t currentOffset;

	/* Notes */
	if (state.elfclass32) {
		currentOffset = sizeof(Elf32_Ehdr) + sizeof(Elf32_Phdr) * (1 + segCnt);
	}
	else {
		currentOffset = sizeof(Elf64_Ehdr) + sizeof(Elf64_Phdr) * (1 + segCnt);
	}

	coredump_dumpPhdr(PT_NOTE, currentOffset, 0, NOTES_SIZE);
	currentOffset += NOTES_SIZE;

	/* Memory */
	if (state.opts.memScope == COREDUMP_MEM_ALL) {
		for (size_t i = 0; i < state.memSegCnt; i++) {
			coredump_dumpPhdr(PT_LOAD, currentOffset, state.memList[i].startAddr,
					state.memList[i].endAddr - state.memList[i].startAddr);
			currentOffset += state.memList[i].endAddr - state.memList[i].startAddr;
		}
	}
	else if (state.opts.memScope == COREDUMP_MEM_EXC_STACK) {
		coredump_dumpPhdr(PT_LOAD, currentOffset, state.threadStacks[0].addr, state.threadStacks[0].size);
		currentOffset += state.threadStacks[0].size;
	}
	else if (state.opts.memScope == COREDUMP_MEM_ALL_STACKS) {
		for (size_t i = 0; i < state.threadCnt; i++) {
			coredump_dumpPhdr(PT_LOAD, currentOffset, state.threadStacks[i].addr, state.threadStacks[i].size);
			currentOffset += state.threadStacks[i].size;
		}
	}
}


int coredump_createFile(char *crashPath)
{
	if (state.opts.savepath == NULL) {
		state.fp = NULL;
		return EINVAL;
	}

	/* Check file count in savepath directory */
	DIR *dir = opendir(state.opts.savepath);
	if (dir == NULL) {
		PRINT_ERR("opendir failed: %s\n", strerror(errno));
		state.fp = NULL;
		return errno;
	}
	int fileCount = 0;
	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL) {
		if (entry->d_type == DT_REG) {
			fileCount++;
		}
	}
	closedir(dir);
	if (state.opts.maxFiles > 0 && fileCount >= state.opts.maxFiles) {
		PRINT_ERR("Maximum number of coredump files reached (%zu)\n", state.opts.maxFiles);
		state.fp = NULL;
		return ENOSPC;
	}

	char *base = strrchr(crashPath, '/');
	if (base != NULL) {
		crashPath = base + 1;
	}
	int n = strlen(state.opts.savepath) + strlen(crashPath) + 22;
	char *path = malloc(n + 1);
	unsigned long long now = time(NULL);
	snprintf(path, n, "%s/%s.%llu", state.opts.savepath, crashPath, now);
	state.fp = fopen(path, "wb");
	free(path);
	if (state.fp == NULL) {
		PRINT_ERR("fopen failed: %s\n", strerror(errno));
		state.fp = NULL;
		return errno;
	}
	return 0;
}


void coredump_dump(coredump_general *crashInfo)
{
	if (state.opts.savepath != NULL && coredump_createFile(crashInfo->path) != 0) {
		PRINT_ERR("Failed to create coredump file, disabling file output!\n");
		free(state.opts.savepath);
		state.opts.savepath = NULL;
	}
	if (state.fp == NULL && state.opts.print == 0) {
		PRINT_ERR("All output methods disabled!\n");
		return;
	}

	state.elfclass32 = crashInfo->type == COREDUMP_TYPE_32;
	state.memSegCnt = crashInfo->memSegCnt;
	state.threadCnt = crashInfo->threadCnt;
	if (state.opts.maxThreads != 0) {
		state.threadCnt = min(state.threadCnt, state.opts.maxThreads);
	}

	state.memList = malloc(sizeof(coredump_memseg) * crashInfo->memSegCnt);
	state.threadStacks = malloc(state.threadCnt * sizeof(state.threadStacks[0]));
	if (state.memList == NULL || state.threadStacks == NULL) {
		PRINT_ERR("malloc failed\n");
		goto err;
	}

	if (coredump_getMemList(sizeof(coredump_memseg) * crashInfo->memSegCnt, state.memList) != 0) {
		PRINT_ERR("Failed to get memory list\n");
		goto err;
	}
	if (coredump_getRelocs(sizeof(state.relocs), state.relocs) != 0) {
		PRINT_ERR("Failed to get relocations\n");
		goto err;
	}
	if (coredump_findAllStacks(crashInfo->tid) != 0) {
		PRINT_ERR("Failed to find stacks\n");
		goto err;
	}

	int segCnt;
	switch (state.opts.memScope) {
		case COREDUMP_MEM_ALL:
			segCnt = crashInfo->memSegCnt;
			break;
		case COREDUMP_MEM_ALL_STACKS:
			segCnt = state.threadCnt;
			break;
		case COREDUMP_MEM_EXC_STACK:
			segCnt = 1;
			break;
		default:
			segCnt = 0;
			break;
	}

	coredump_init(crashInfo->path, crashInfo->signo);
	coredump_dumpElfHeader(segCnt);
	coredump_dumpAllPhdrs(segCnt);

	if (coredump_dumpAllThreadsNotes(crashInfo->tid) != 0) {
		PRINT_ERR("Failed to dump thread notes\n");
		goto err;
	}

	elf_proc_aux procAux;
	hal_createProcAuxNotes(&procAux, &state.opts);
	coredump_encodeChunk((__u8 *)&procAux, hal_procAuxNotesSize(&state.opts));

	coredump_dumpLMANote();

	/* MEMORY */
	switch (state.opts.memScope) {
		case COREDUMP_MEM_ALL:
			if (coredump_dumpAllMem() != 0) {
				PRINT_ERR("Failed to dump all memory\n");
				goto err;
			}
			break;
		case COREDUMP_MEM_EXC_STACK:
			/* exception thread is put into threadInfo at index 0 */
			if (coredump_dumpMemory(state.threadStacks[0].addr, state.threadStacks[0].size) != 0) {
				PRINT_ERR("Failed to dump exception stack memory\n");
				goto err;
			}
			break;
		case COREDUMP_MEM_ALL_STACKS:
			for (int i = 0; (i < state.threadCnt); i++) {
				if (coredump_dumpMemory(state.threadStacks[i].addr, state.threadStacks[i].size) != 0) {
					PRINT_ERR("Failed to dump thread stack memory for %d. thread\n", i);
					goto err;
				}
			}
			break;
		default:
			break;
	}

	coredump_finalize();
err:
	free(state.memList);
	free(state.threadStacks);

	if (state.fp != NULL) {
		fclose(state.fp);
	}
}


static int initSaveDir(char *path)
{
	struct stat st;
	while (stat("/", &st) < 0) {
		if (errno == ENOENT) {
			usleep(500000);
		}
		else {
			PRINT_ERR("stat failed with: %s\n", strerror(errno));
			return errno;
		}
	}

	if ((mkdir(path, 0) != 0) && (errno != EEXIST)) {
		return errno;
	}
	return 0;
}


static void _settingsthr(void *arg)
{
	for (;;) {
		msg_t msg;
		msg_rid_t rid;
		if (msgRecv(state.settingsDev.port, &msg, &rid) < 0) {
			PRINT_ERR("portRecv failed\n");
			break;
		}

		msg.o.err = EOK;

		if (msg.type == mtSetAttr) {
			pthread_mutex_lock(&state.mutex);
			switch (msg.i.attr.type) {
				case COREDUMP_ATTR_PATH:
					if (msg.i.attr.val == 0) {
						free(state.opts.savepath);
						state.opts.savepath = NULL;
						break;
					}
					if (msg.i.size == 0) {
						msg.o.err = ENOENT;
						break;
					}
					int ret = initSaveDir((char *)msg.i.data);
					if (ret != 0) {
						msg.o.err = ret;
						break;
					}
					free(state.opts.savepath);
					state.opts.savepath = strndup((char *)msg.i.data, msg.i.size);
					if (state.opts.savepath == NULL) {
						msg.o.err = ENOMEM;
					}
					break;
				case COREDUMP_ATTR_MAX_THREADS:
					if (msg.i.attr.val < 0) {
						msg.o.err = EINVAL;
					}
					else {
						state.opts.maxThreads = msg.i.attr.val;
					}
					break;
				case COREDUMP_ATTR_MAX_STACK_SIZE:
					if (msg.i.attr.val < 0) {
						msg.o.err = EINVAL;
					}
					else {
						state.opts.maxStackSize = msg.i.attr.val;
					}
					break;
				case COREDUMP_ATTR_MEM_SCOPE:
					if (msg.i.attr.val < COREDUMP_MEM_NONE || msg.i.attr.val > COREDUMP_MEM_ALL) {
						msg.o.err = EINVAL;
					}
					else {
						state.opts.memScope = msg.i.attr.val;
					}
					break;
				case COREDUMP_ATTR_FP_CONTEXT:
					if (msg.i.attr.val < 0 || msg.i.attr.val > 1) {
						msg.o.err = EINVAL;
					}
					else {
						state.opts.fpContext = msg.i.attr.val;
					}
					break;
				case COREDUMP_ATTR_PRINT:
					if (msg.i.attr.val < 0 || msg.i.attr.val > 1) {
						msg.o.err = EINVAL;
					}
					else {
						state.opts.print = msg.i.attr.val;
					}
					break;
				case COREDUMP_ATTR_PRINT_SLEEP:
					if (msg.i.attr.val < 0) {
						msg.o.err = EINVAL;
					}
					else {
						state.opts.printSleep = msg.i.attr.val;
					}
					break;
				case COREDUMP_ATTR_MAX_FILES:
					if (msg.i.attr.val < 0) {
						msg.o.err = EINVAL;
					}
					else {
						state.opts.maxFiles = msg.i.attr.val;
					}
					break;
				default:
					msg.o.err = ENOSYS;
					break;
			}
			pthread_mutex_unlock(&state.mutex);
		}
		else if (msg.type == mtGetAttrAll) {
			memcpy(msg.o.data, &state.opts, min(sizeof(state.opts), msg.o.size));
			msg.o.attr.val = sizeof(state.opts);
			if (state.opts.savepath != NULL) {
				memcpy(msg.o.data + sizeof(state.opts), state.opts.savepath, min(strlen(state.opts.savepath) + 1, msg.o.size - sizeof(state.opts)));
				msg.o.attr.val += strlen(state.opts.savepath) + 1;
			}
		}
		else {
			msg.o.err = EOPNOTSUPP;
		}

		msgRespond(state.settingsDev.port, &msg, rid);
	}
}


void initSettings(void)
{
	state.opts = defaultOpts;

	int ret = initSaveDir(defaultOpts.savepath);
	if (ret == 0) {
		state.opts.savepath = strdup(defaultOpts.savepath);
	}
	else {
		PRINT_ERR("Failed to create save directory (%s), disabled file saving.\n", strerror(ret));
		state.opts.savepath = NULL;
	}
}


int main(int argc, char *argv[])
{
#ifdef COREDUMP_DISABLE
	return 1;
#endif

	if (argc > 1) {
		return settings_parseArgs(argc, argv);
	}

	if (portCreate(&state.settingsDev.port) < 0) {
		PRINT_ERR("portCreate failed\n");
		return 1;
	}
	state.settingsDev.id = 0;
	if (create_dev(&state.settingsDev, COREDUMP_SETTINGS_DEV) < 0) {
		PRINT_ERR("Can't create device! Isn't another instance already running?\n");
		portDestroy(state.settingsDev.port);
		return 1;
	}

	if (portCreate(&state.memRecvPort) < 0) {
		PRINT_ERR("portCreate failed\n");
		portDestroy(state.settingsDev.port);
		return 1;
	}

	pthread_mutex_init(&state.mutex, NULL);
	initSettings();

	pthread_t settingsThread;
	if (pthread_create(&settingsThread, NULL, (void *)_settingsthr, NULL) != 0) {
		PRINT_ERR("pthread_create failed\n");
		portDestroy(state.memRecvPort);
		portDestroy(state.settingsDev.port);
		return 1;
	}

	for (;;) {
		coredump_general crashInfo;
		if (coredump_waitForCrash(&crashInfo) != 0) {
			break;
		}

		pthread_mutex_lock(&state.mutex); /* lock settings during dump */
		coredump_dump(&crashInfo);
		pthread_mutex_unlock(&state.mutex);

		coredump_closeCrash();
	}

	portDestroy(state.memRecvPort);
	portDestroy(state.settingsDev.port);
	pthread_cancel(settingsThread);
	unlink(COREDUMP_SETTINGS_DEV);
	return 0;
}
