/*
 * Phoenix-RTOS
 *
 * nslookup - make DNS queries
 *
 * Copyright 2021 Phoenix Systems
 * Author: Maciej Purski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */


#include <arpa/inet.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>

#include "../psh.h"


void psh_nslookupinfo(void)
{
	printf("queries domain name servers");
}


static uint32_t *psh_aiToAddr(struct addrinfo *ai)
{
	switch (ai->ai_family) {
		case AF_INET:
			return (uint32_t *)&((struct sockaddr_in *)ai->ai_addr)->sin_addr;
		case AF_INET6:
			return (uint32_t *)&((struct sockaddr_in6 *)ai->ai_addr)->sin6_addr;
		default:
			return NULL;
	}
}


int psh_nslookup(int argc, char **argv)
{
	struct addrinfo *res, *cur;
	struct addrinfo hints = { 0 };
	char hostaddr[INET6_ADDRSTRLEN] = { 0 };

	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_CANONNAME;

	if (argc < 2) {
		fprintf(stderr, "nslookup: hostname required!\n");
		return -EINVAL;
	}

	if (getaddrinfo(argv[1], NULL, &hints, &res) != 0) {
		fprintf(stderr, "nslookup: Can't resolve hostname!\n");
		return -EINVAL;
	}

	cur = res;
	while (cur) {
		if (inet_ntop(cur->ai_family, psh_aiToAddr(cur), hostaddr, sizeof(hostaddr)) == NULL) {
			fprintf(stderr, "nslookup: Error while converting address!\n");
			break;
		}
		printf("%-10s %s\n", "Name: ", cur->ai_canonname);
		printf("%-10s %s\n", "Address: ", hostaddr);
		cur = cur->ai_next;
	}

	freeaddrinfo(res);

	return EOK;
}


void __attribute__((constructor)) nslookup_registerapp(void)
{
	static psh_appentry_t app = { .name = "nslookup", .run = psh_nslookup, .info = psh_nslookupinfo };
	psh_registerapp(&app);
}
