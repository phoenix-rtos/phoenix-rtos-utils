/*
 * Phoenix-RTOS
 *
 * ping - ICMP request / response
 *
 * Copyright 2021 Phoenix Systems
 * Author: Maciej Purski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/select.h>
#include <netinet/ip_icmp.h>
#include <sys/time.h>
#include <errno.h>

#include "../psh.h"

static struct {
	struct sockaddr_in raddr;
	int seq;
	int cnt;
	int ttl;
	int af;
	int interval; /* ms */
	int timeout;  /* ms */
	int reqsz;
	int respsz;
} ping_common;


void psh_pinginfo(void)
{
	printf("ICMP ECHO requests");
	return;
}


static void ping_help(void)
{
	printf("Usage: ping [options] address\n");
	printf("Options\n");
	printf("  -h:  prints help\n");
	printf("  -c:  count, number of requests to be sent, default 5\n");
	printf("  -i:  interval in milliseconds, minimum 200 ms, default 1000\n");
	printf("  -t:  IP Time To Live, default 64\n");
	printf("  -s:  payload size, default 56, maximum 2040\n");
	printf("  -W:  socket timetout, default 2000\n\n");
}


static uint16_t ping_chksum(uint8_t *data, int len)
{
	uint32_t sum = 0;
	int i;

	for (i = 0; i < len; i += 2)
		sum += (data[i] << 8) + data[i + 1];

	if (len % 2 != 0)
		sum += data[len - 1] << 8;

	sum = (sum >> 16) + (sum & 0x0000FFFF);
	sum = (sum >> 16) + (sum & 0x0000FFFF);

	return ~(uint16_t)sum;
}


static int ping_sockconf(void)
{
	struct timeval tv;
	int fd;

	if ((fd = socket(ping_common.af, SOCK_RAW, IPPROTO_ICMP)) < 0) {
		fprintf(stderr, "ping: Can't open socket!\n");
		return -EIO;
	}

	if (setsockopt(fd, IPPROTO_IP, IP_TTL, &ping_common.ttl, sizeof(ping_common.ttl)) != 0) {
		close(fd);
		fprintf(stderr, "ping: Can't set TTL!\n");
		return -EIO;
	}

	tv.tv_sec = ping_common.timeout / 1000;
	tv.tv_usec = (ping_common.timeout % 1000) * 1000;
	if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0) {
		close(fd);
		fprintf(stderr, "ping: Can't set socket timeout!\n");
		return -EIO;
	}

	return fd;
}


static void ping_reqinit(char *data, int len)
{
	struct icmphdr *hdr = (struct icmphdr *)data;
	int i;

	hdr->type = ICMP_ECHO;
	hdr->un.echo.id = htons(getpid());

	for (i = 0; i < len - sizeof(struct icmphdr); i++)
		data[sizeof(struct icmphdr) + i] = (char)i;
}


static int ping_echo(int fd, char *data, int len)
{
	struct icmphdr *hdr = (struct icmphdr *)data;

	hdr->un.echo.sequence = htons(ping_common.seq++);
	hdr->checksum = 0;
	hdr->checksum = htons(ping_chksum((uint8_t *)data, len));

	if (sendto(fd, data, len, 0, (struct sockaddr *)&ping_common.raddr, sizeof(ping_common.raddr)) != len)
		return -1;

	return 0;
}


static int ping_reply(int fd, char *data, size_t len, char *addrstr, int addrlen)
{
	struct sockaddr_in rsin;
	socklen_t rlen = sizeof(rsin);
	struct icmphdr *icmphdr;
	int bytes;
	uint16_t chksum;

	if ((bytes = recvfrom(fd, data, len, 0, (struct sockaddr *)&rsin, &rlen)) <= 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			fprintf(stderr, "Host timeout\n");
		else
			fprintf(stderr, "ping: Fail to receive packet on socket!\n");
		return -1;
	}

	if (bytes < sizeof(struct iphdr) + sizeof(struct icmphdr)) {
		fprintf(stderr, "ping: Received msg too short (%d)!\n", bytes);
		return -1;
	}

	bytes -= sizeof(struct iphdr);

	if (inet_ntop(ping_common.af, &rsin.sin_addr, addrstr, addrlen) == NULL) {
		fprintf(stderr, "ping: Invalid address received!\n");
		return -1;
	}

	if (memcmp(&rsin.sin_addr, &ping_common.raddr.sin_addr, sizeof(rsin.sin_addr))) {
		fprintf(stderr, "ping: Response from invalid address: %s!\n", addrstr);
		return -1;
	}

	icmphdr = (struct icmphdr *)(data + sizeof(struct iphdr));
	chksum = ntohs(icmphdr->checksum);
	icmphdr->checksum = 0;

	if (ntohs(icmphdr->un.echo.sequence) != ping_common.seq - 1) {
		fprintf(stderr, "ping: Response out of sequence!\n");
		return -1;
	}

	if (ping_chksum((uint8_t *)icmphdr, bytes) != chksum) {
		fprintf(stderr, "ping: Response invalid checksum!\n");
		return -1;
	}

	return bytes;
}


static int ping_echoreply(int fd, char *req, char *resp)
{
	struct icmphdr *icmphdr;
	struct iphdr *iphdr;
	time_t sent, elapsed;
	struct timespec ts;
	char addrstr[INET_ADDRSTRLEN];
	char timestr[32];
	int bytes;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	sent = ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
	if (ping_echo(fd, req, ping_common.reqsz) < 0) {
		fprintf(stderr, "ping: Fail to send a packet!\n");
		return -1;
	}

	bzero(resp, ping_common.respsz);
	if ((bytes = ping_reply(fd, resp, ping_common.respsz, addrstr, INET_ADDRSTRLEN)) < 0)
		return -1;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	elapsed = ts.tv_sec * 1000000 + ts.tv_nsec / 1000 - sent;
	if ((elapsed % 1000) / 10 == 0)
		snprintf(timestr, sizeof(timestr), "%llu ms", elapsed / 1000);
	else
		snprintf(timestr, sizeof(timestr), "%llu.%02llu ms", elapsed / 1000, (elapsed % 1000) / 10);

	iphdr = (struct iphdr *)resp;
	icmphdr = (struct icmphdr *)(resp + sizeof(struct iphdr));

	printf("%d bytes received from %s: ttl=%u icmp_seq=%u time=%s\n",
		bytes, addrstr, iphdr->ttl, ntohs(icmphdr->un.echo.sequence), timestr);

	return 0;
}


int psh_ping(int argc, char **argv)
{
	char *resp, *req;
	int fd, c, ret = 0;
	char *end;

	ping_common.cnt = 5;
	ping_common.interval = 1000;
	ping_common.timeout = 2000;
	ping_common.seq = 1;
	ping_common.ttl = 64;
	ping_common.reqsz = 56 + sizeof(struct icmphdr);
	ping_common.respsz = ping_common.reqsz + sizeof(struct iphdr);
	ping_common.af = AF_INET;
	ping_common.raddr.sin_family = AF_INET; /* TODO: handle ip6 */

	while ((c = getopt(argc, argv, "i:t:c:W:s:h")) != -1) {
		switch (c) {
			case 'c':
				ping_common.cnt = strtoul(optarg, &end, 10);
				if (*end != '\0' || ping_common.cnt <= 0) {
					fprintf(stderr, "ping: Wrong count value!\n");
					return 2;
				}
				break;
			case 't':
				ping_common.ttl = strtoul(optarg, &end, 10);
				if (*end != '\0' || ping_common.ttl <= 0) {
					fprintf(stderr, "ping: Wrong ttl value!\n");
					return 2;
				}
				break;
			case 'i':
				ping_common.interval = strtoul(optarg, &end, 10);
				if (*end != '\0' || ping_common.interval < 0) {
					fprintf(stderr, "ping: Wrong interval value!\n");
					return 2;
				}
				break;
			case 'W':
				ping_common.timeout = strtoul(optarg, &end, 10);
				if (*end != '\0' || ping_common.timeout <= 100) {
					fprintf(stderr, "ping: Wrong timeout value!\n");
					return 2;
				}
				break;
			case 's':
				ping_common.reqsz = strtoul(optarg, &end, 10) + sizeof(struct icmphdr);
				ping_common.respsz = sizeof(struct iphdr) + ping_common.reqsz;
				if (*end != '\0' || ping_common.reqsz > 2040) {
					fprintf(stderr, "ping: Wrong payload len\n");
					return 2;
				}
				break;
			default:
			case 'h':
				ping_help();
				return 0;
		}
	}

	if (argc - optind != 1) {
		fprintf(stderr, "ping: Expected address!\n");
		return 2;
	}

	if (inet_pton(ping_common.af, argv[optind], &ping_common.raddr.sin_addr) != 1) {
		fprintf(stderr, "ping: Invalid IP address!\n");
		return 2;
	}

	if ((req = malloc(ping_common.reqsz)) == NULL) {
		fprintf(stderr, "ping: Out of memory!\n");
		return 2;
	}

	if ((resp = malloc(ping_common.respsz)) == NULL) {
		fprintf(stderr, "ping: Out of memory!\n");
		free(req);
		return 2;
	}

	if ((fd = ping_sockconf()) <= 0) {
		free(req);
		free(resp);
		return 2;
	}

	ping_reqinit(req, ping_common.reqsz);

	while (ping_common.cnt-- && !psh_common.sigint) {
		if (ping_echoreply(fd, req, resp) != 0) {
			ret = 1;
			break;
		}

		if (ping_common.cnt > 0)
			usleep(1000 * ping_common.interval);
	}

	close(fd);
	free(req);
	free(resp);

	return ret;
}


void __attribute__((constructor)) ping_registerapp(void)
{
	static psh_appentry_t app = { .name = "ping", .run = psh_ping, .info = psh_pinginfo };
	psh_registerapp(&app);
}
