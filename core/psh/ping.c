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

#include "psh.h"

static struct {
	struct sockaddr_in raddr;
	int seq;
	int cnt;
	int ttl;
	int af;
	int interval; /* ms */
	int timeout; /* ms */
} ping_common;


void psh_pinginfo(void)
{
	printf("  ping    - ICMP ECHO requests\n");
	return;
}


static void psh_ping_help(void)
{
	printf("Usage: ping [options] address\n");
	printf("Options\n");
	printf("  -h:  prints help\n");
	printf("  -c:  count, number of requests to be sent, default 5\n");
	printf("  -i:  interval in milliseconds, minimum 200 ms, default 1000\n");
	printf("  -t:  IP Time To Live, default 64\n");
	printf("  -W:  socket timetout, default 500\n\n");
}


static uint16_t psh_ping_chksum(uint8_t *data, int len)
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


static int psh_ping_socketConfig(void)
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


static void psh_ping_reqInit(uint8_t *data, int len)
{
	struct icmphdr *hdr = (struct icmphdr *) data;
	int i;

	hdr->type = ICMP_ECHO;
	hdr->un.echo.id = htons(getpid());

	for (i = 0; i < len - sizeof(struct icmphdr); i++)
		data[sizeof(struct icmphdr) + i] = (uint8_t) i;
}


static int psh_ping_send(int fd, uint8_t *data, int len)
{
	struct icmphdr *hdr = (struct icmphdr *) data;

	hdr->un.echo.sequence = htons(ping_common.seq++);
	hdr->checksum = 0;
	hdr->checksum = htons(psh_ping_chksum(data, len));

	if (sendto(fd, data, len, 0, (struct sockaddr *) &ping_common.raddr,
	           sizeof(ping_common.raddr)) != len)
		return -1;

	return 0;
}


static int psh_ping_recv(int fd, uint8_t *data, int len, char *infostr, int infostrlen)
{
	struct sockaddr_in rsin;
	socklen_t rlen;
	struct iphdr *iphdr;
	struct icmphdr *icmphdr;
	int bytes;
	char addrstr[INET_ADDRSTRLEN];
	uint16_t chksum;

	if ((bytes = recvfrom(fd, data, len, 0, (struct sockaddr *) &rsin, &rlen)) <= 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			fprintf(stderr, "Host unreachable\n");
		else
			fprintf(stderr, "ping: Fail to receive packet on socket!\n");
		return -1;
	}

	if (bytes < sizeof(struct iphdr) + sizeof(struct icmphdr)) {
		fprintf(stderr, "ping: Received msg too short (%d)!\n", bytes);
		return -1;
	}

	if (inet_ntop(ping_common.af, &rsin.sin_addr, addrstr, sizeof(addrstr)) == NULL) {
		fprintf(stderr, "ping: Invalid address received!\n");
		return -1;
	}

	if (memcmp(&rsin.sin_addr, &ping_common.raddr.sin_addr, sizeof(rsin.sin_addr))) {
		fprintf(stderr, "ping: Response from invalid address: %s!\n", addrstr);
		return -1;
	}

	iphdr = (struct iphdr *) data;
	icmphdr = (struct icmphdr *) (data + sizeof(struct iphdr));
	chksum = ntohs(icmphdr->checksum);
	icmphdr->checksum = 0;

	if (psh_ping_chksum(data, bytes) != chksum) {
		fprintf(stderr, "ping: Response invalid checksum!\n");
		return -1;
	}

	return snprintf(infostr, infostrlen, "%d bytes received from %s: ttl=%u icmp_seq=%u",
	                bytes - sizeof(struct iphdr), addrstr, iphdr->ttl, ntohs(icmphdr->un.echo.sequence));
}


int psh_ping(int argc, char **argv)
{
	uint8_t resp[128] = { 0 };
	uint8_t req[64] = { 0 };
	char infostr[80];
	time_t sent, elapsed;
	int fd, c;
	char *end;
	struct timespec ts;

	ping_common.cnt = 5;
	ping_common.interval = 1000;
	ping_common.timeout = 500;
	ping_common.seq = 1;
	ping_common.ttl = 64;
	ping_common.af = AF_INET;
	ping_common.raddr.sin_family = AF_INET; /* TODO: handle ip6 */

	while ((c = getopt(argc, argv, "i:t:c:W:h")) != -1) {
		switch (c) {
		case 'c':
			ping_common.cnt = strtoul(optarg, &end, 10);
			if (*end != '\0' || ping_common.cnt <= 0) {
				fprintf(stderr, "ping: Wrong count value!\n");
				return -EINVAL;
			}
			break;
		case 't':
			ping_common.ttl = strtoul(optarg, &end, 10);
			if (*end != '\0' || ping_common.ttl <= 0) {
				fprintf(stderr, "ping: Wrong ttl value!\n");
				return -EINVAL;
			}
			break;
		case 'i':
			ping_common.interval = strtoul(optarg, &end, 10);
			if (*end != '\0' || ping_common.interval < 0) {
				fprintf(stderr, "ping: Wrong interval value!\n");
				return -EINVAL;
			}
			break;
		case 'W':
			ping_common.timeout = strtoul(optarg, &end, 10);
			if (*end != '\0' || ping_common.timeout <= 200) {
				fprintf(stderr, "ping: Wrong timeout value!\n");
				return -EINVAL;
			}
			break;
		default:
		case 'h':
			psh_ping_help();
			return 0;
		}
	}

	if (argc - optind != 1) {
		fprintf(stderr, "ping: Expected address!\n");
		return -EINVAL;
	}

	if (inet_pton(ping_common.af, argv[optind], &ping_common.raddr.sin_addr) != 1) {
		fprintf(stderr, "ping: Invalid IP address!\n");
		return -EINVAL;
	}

	if ((fd = psh_ping_socketConfig()) <= 0)
		return -EIO;

	psh_ping_reqInit(req, sizeof(req));

	while (ping_common.cnt-- && !psh_common.sigint) {
		clock_gettime(CLOCK_MONOTONIC, &ts);
		sent = ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
		if (psh_ping_send(fd, req, sizeof(req)) < 0)	{
			close(fd);
			fprintf(stderr, "ping: Fail to send a packet!\n");
			return -EINVAL;
		}

		bzero(infostr, sizeof(infostr));
		bzero(resp, sizeof(resp));
		if (psh_ping_recv(fd, resp, sizeof(resp), infostr, sizeof(infostr)) > 0) {
			clock_gettime(CLOCK_MONOTONIC, &ts);
			elapsed = ts.tv_sec * 1000000 + ts.tv_nsec / 1000 - sent;
			printf("%s time=%lu.%02lu ms\n", infostr, (unsigned long) (elapsed / 1000),
			                                 (unsigned long) ((elapsed % 1000) / 10));
		}

		if (ping_common.cnt > 0)
			usleep(1000 * ping_common.interval);
	}
	close(fd);

	return 0;
}
