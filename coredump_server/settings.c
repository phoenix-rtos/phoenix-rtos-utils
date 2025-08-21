#include "settings.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/msg.h>


static int settings_lookupDev(oid_t *oid)
{
	if (lookup(COREDUMP_SETTINGS_DEV, NULL, oid) < 0) {
		printf("Failed to lookup settings device '%s'\n", COREDUMP_SETTINGS_DEV);
		return -ENOENT;
	}
	return 0;
}


static int settings_nameToAttr(const char *name)
{
	if (strcmp(name, "MAX_THREADS") == 0) {
		return COREDUMP_ATTR_MAX_THREADS;
	}
	else if (strcmp(name, "MAX_STACK_SIZE") == 0) {
		return COREDUMP_ATTR_MAX_STACK_SIZE;
	}
	else if (strcmp(name, "MEM_SCOPE") == 0) {
		return COREDUMP_ATTR_MEM_SCOPE;
	}
	else if (strcmp(name, "FP_CONTEXT") == 0) {
		return COREDUMP_ATTR_FP_CONTEXT;
	}
	else if (strcmp(name, "PRINT") == 0) {
		return COREDUMP_ATTR_PRINT;
	}
	else if (strcmp(name, "PRINT_SLEEP") == 0) {
		return COREDUMP_ATTR_PRINT_SLEEP;
	}
	else if (strcmp(name, "PATH") == 0) {
		return COREDUMP_ATTR_PATH;
	}
	else if (strcmp(name, "MAX_FILES") == 0) {
		return COREDUMP_ATTR_MAX_FILES;
	}
	return -EINVAL;
}


static char *settings_memscopeName(int scope)
{
	switch (scope) {
		case COREDUMP_MEM_NONE:
			return "none";
		case COREDUMP_MEM_EXC_STACK:
			return "exception thread stack";
		case COREDUMP_MEM_ALL_STACKS:
			return "all threads stacks";
		case COREDUMP_MEM_ALL:
			return "all memory";
		default:
			return "invalid";
	}
}


static void settings_read(void)
{
	static const int SAVEPATH_MAX = 128;
	static const size_t OUT_SIZE = sizeof(coredump_opts_t) + SAVEPATH_MAX;
	oid_t oid;
	if (settings_lookupDev(&oid) != 0) {
		return;
	}

	msg_t msg;
	msg.oid = oid;
	msg.type = mtGetAttrAll;
	msg.i.size = 0;
	msg.i.data = NULL;
	msg.o.size = OUT_SIZE;
	msg.o.data = malloc(OUT_SIZE);
	int ret = msgSend(oid.port, &msg);
	if (ret != 0) {
		printf("Failed to read settings: '%s'\n", strerror(-ret));
		return;
	}
	if (msg.o.err != 0) {
		printf("Failed to read settings: '%s'\n", strerror(msg.o.err));
		return;
	}

	coredump_opts_t *opts = (coredump_opts_t *)msg.o.data;
	if (msg.o.attr.val == sizeof(coredump_opts_t)) {
		opts->savepath = NULL;
	}
	else {
		opts->savepath = (char *)(opts + 1);
	}

	printf("Current settings:\n");
	printf("  Max Threads: %zu\n", opts->maxThreads);
	printf("  Max Stack Size: 0x%zx\n", opts->maxStackSize);
	printf("  Memory Scope: %s (%d)\n", settings_memscopeName(opts->memScope), opts->memScope);
	printf("  FP Context: %s\n", opts->fpContext ? "Enabled" : "Disabled");
	printf("  Max Memory Chunk: %zu\n", opts->maxMemChunk);
	printf("  Print: %s\n", opts->print ? "Enabled" : "Disabled");
	printf("  Print Sleep: %u us\n", opts->printSleep);
	if (opts->savepath == NULL) {
		printf("  Save Path: Disabled\n");
	}
	else {
		printf("  Save Path: %.*s%s\n", SAVEPATH_MAX, opts->savepath, msg.o.attr.val > OUT_SIZE ? "..." : "");
	}
	printf("  Max Files: %zu\n", opts->maxFiles);
	printf("\n");

	free(msg.o.data);
}


static void settings_set(char *opt, char *val)
{
	oid_t oid;
	if (settings_lookupDev(&oid) != 0) {
		return;
	}

	int attr = settings_nameToAttr(opt);
	if (attr < 0) {
		printf("Unknown setting '%s'\n", opt);
		return;
	}

	int value = atoi(val);

	msg_t msg;
	msg.oid = oid;
	msg.type = mtSetAttr;
	msg.i.attr.type = attr;
	if (attr == COREDUMP_ATTR_PATH && *val != '0') {
		msg.i.attr.val = 1;
		msg.i.size = strlen(val) + 1;
		msg.i.data = val;
	}
	else {
		msg.i.attr.val = value;
		msg.i.size = 0;
		msg.i.data = NULL;
	}
	msg.o.size = 0;
	msg.o.data = NULL;
	int ret = msgSend(oid.port, &msg);
	if (ret != 0) {
		printf("Failed to set setting '%s': %s\n", opt, strerror(-ret));
		return;
	}
	if (msg.o.err != 0) {
		printf("Failed to set setting '%s': %s\n", opt, strerror(msg.o.err));
		return;
	}

	if (attr == COREDUMP_ATTR_PATH) {
		printf("Changed '%s' to '%s'\n", opt, val);
	}
	else if (attr == COREDUMP_ATTR_MEM_SCOPE) {
		printf("Changed '%s' to '%s' (%d)\n", opt, settings_memscopeName(value), value);
	}
	else {
		printf("Changed '%s' to '%d'\n", opt, value);
	}
}


static int settings_parseOption(char *arg[])
{
	if (strcmp(arg[0], "-h") == 0 || strcmp(arg[0], "--help") == 0) {
		printf("Options:\n");
		printf("  -h, --help\t\tShow this help message\n");
		printf("  -s, --set <name> <value>\tSet coredump server setting\n");
		printf("  -g, --get\tGet current coredump server settings\n");
		printf("Settings:\n");
		printf("  MAX_THREADS, MAX_STACK_SIZE, MEM_SCOPE, FP_CONTEXT, PRINT, PRINT_SLEEP, PATH, MAX_FILES\n");
		printf("Example: coredump_server -s MAX_THREADS 8\n");
		printf("         coredump_server -s PATH 0\n");
		printf("         coredump_server -s PATH \"/coredumps\"\n");
		printf("\n");
		return 1;
	}

	if (strcmp(arg[0], "-g") == 0 || strcmp(arg[0], "--get") == 0) {
		settings_read();
		return 1;
	}

	if (strcmp(arg[0], "-s") == 0 || strcmp(arg[0], "--set") == 0) {
		if (arg[1] == NULL) {
			printf("Error: Missing option name for -s/--set\n");
			return -EINVAL;
		}
		if (arg[2] == NULL) {
			printf("Error: Missing value for option '%s'\n", arg[1]);
			return -EINVAL;
		}
		settings_set(arg[1], arg[2]);
		return 3;
	}
	return -EINVAL;
}


int settings_parseArgs(int argc, char *argv[])
{
	int i = 1;
	int ret;
	while (i < argc) {
		ret = settings_parseOption(&argv[i]);
		if (ret < 0) {
			return ret;
		}
		i += ret;
	}
	return 0;
}
