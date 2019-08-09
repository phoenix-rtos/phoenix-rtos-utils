/*
 * Phoenix-RTOS
 *
 * Packet Filter utility
 *
 * Copyright 2019 Phoenix Systems
 * Author: Aleksander Kaminski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/msg.h>
#include "../../libsystem/libsystem_pfparser.h"


msg_t msg;


int usage(const char *pname)
{
	fprintf(stderr, "usage: %s <pf rules file>\n", pname);
	return -1;
}


int main(int argc, char *argv[])
{
	pfrule_t *list, *rule;
	pfrule_array_t *array;
	size_t rulecnt, writelen;
	int i;
	oid_t oid;

	if (argc != 2)
		return usage(argv[0]);

	while (lookup("/dev/pf", NULL, &oid) < 0)
		usleep(250 * 1000);

	if ((list = pfparser_parseFile(argv[1])) == NULL) {
		fprintf(stderr, "%s: Failed to parse file %s\n", argv[0], argv[1]);
		return -1;
	}

	for (rulecnt = 0, rule = list; rule != NULL; rule = rule->next)
		++rulecnt;

	fprintf(stderr, "%s: Parsed %u rules\n", argv[0], rulecnt);

	writelen = sizeof(pfrule_array_t) + sizeof(pfrule_t) * rulecnt;

	if ((array = malloc(writelen)) == NULL) {
		fprintf(stderr, "%s: Out of memory\n", argv[0]);
		return -1;
	}

	for (i = 0, rule = list; rule != NULL; rule = rule->next)
		memcpy(&array->array[i++], rule, sizeof(pfrule_t));

	array->len = rulecnt;

	msg.type = mtWrite;
	msg.i.io.oid = oid;
	msg.i.io.offs = 0;
	msg.i.io.len = writelen;
	msg.i.io.mode = 0;
	msg.i.data = array;
	msg.i.size = writelen;
	msg.o.data = NULL;
	msg.o.size = 0;

	if (msgSend(oid.port, &msg) < 0) {
		fprintf(stderr, "%s: Failed to send message\n", argv[0]);
		return -1;
	}

	if (msg.o.io.err < 0) {
		fprintf(stderr, "%s: Configuration failed (code %d)\n", argv[0], msg.o.io.err);
		return -1;
	}

	fprintf(stderr, "%s: Configuration done\n", argv[0]);

	return 0;
}
