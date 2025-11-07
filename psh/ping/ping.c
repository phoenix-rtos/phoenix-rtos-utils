/*
 * Phoenix-RTOS
 *
 * ping - ICMP request / response
 *
 * Copyright 2021, 2023 Phoenix Systems
 * Author: Maciej Purski, Gerard Swiderski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip_icmp.h>
#include <netinet/icmp6.h>
#include <netinet/ip.h>
#include <netdb.h>
#include <sys/minmax.h>

#include "../psh.h"

#define MAX_ECHO_PAYLOAD 2032U

static struct {
	struct sockaddr_in raddr;
#ifdef PSH_IPV6_SUPPORT
	struct sockaddr_in6 raddr6;
	uint16_t dataChksum;
	uint8_t dataChksumRdy;
#endif
	int seq;
	int cnt;
	int ttl;
	int af;
	int interval; /* ms */
	int timeout;  /* ms */
	int reqsz;
	int respsz;
	uint16_t myid;
	uint8_t icmpProto; /* ICMP or ICMPv6 protocol */
} pingCommon;


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
	printf("  -i:  interval in milliseconds, default 1000\n");
	printf("  -t:  IP Time To Live, default 64\n");
	printf("  -s:  payload size, default 56, maximum %u\n", MAX_ECHO_PAYLOAD);
#ifdef PSH_IPV6_SUPPORT
	printf("  -6:  use IPv6\n");
#endif
	printf("  -W:  socket timeout, default 2000\n\n");
}


static uint16_t ping_chksum(uint8_t *data, int len)
{
	uint32_t sum = 0;
	int i;

	for (i = 0; i < len; i += 2) {
		sum += (data[i] << 8) + data[i + 1];
	}

	if (len % 2 != 0)
		sum += data[len - 1] << 8;

	sum = (sum >> 16) + (sum & 0x0000FFFF);
	sum = (sum >> 16) + (sum & 0x0000FFFF);

	return ~(uint16_t)sum;
}


static int ping_sockconf(void)
{
	struct timeval tv;
	int fd, ret;

	fd = socket(pingCommon.af, SOCK_RAW, pingCommon.icmpProto);
	if (fd < 0) {
		fprintf(stderr, "ping: Can't open socket!\n");
		return -EIO;
	}

	ret = setsockopt(fd, IPPROTO_IP, IP_TTL, &pingCommon.ttl, sizeof(pingCommon.ttl));
	if (ret != 0) {
		close(fd);
		fprintf(stderr, "ping: Can't set TTL!\n");
		return -EIO;
	}

	tv.tv_sec = pingCommon.timeout / 1000;
	tv.tv_usec = (pingCommon.timeout % 1000) * 1000;
	ret = setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	if (ret != 0) {
		close(fd);
		fprintf(stderr, "ping: Can't set socket timeout!\n");
		return -EIO;
	}

	return fd;
}


static void ping_reqinit(uint8_t *data, int len)
{
	struct icmphdr *hdr = (struct icmphdr *)data;

	hdr->type = ICMP_ECHO;
	pingCommon.myid = getpid();
	hdr->un.echo.id = htons(pingCommon.myid);

	for (unsigned int i = 0; i < len - sizeof(struct icmphdr); i++)
		data[sizeof(struct icmphdr) + i] = (uint8_t)i;
}


#ifdef PSH_IPV6_SUPPORT
static void ping_reqinit6(uint8_t *data, int len)
{
	struct icmp6_hdr *hdr = (struct icmp6_hdr *)data;

	hdr->icmp6_type = ICMP6_ECHO_REQUEST;
	hdr->icmp6_code = 0;
	hdr->icmp6_cksum = 0;
	pingCommon.myid = getpid();
	hdr->icmp6_dataun.echo.id = htons(pingCommon.myid);
	hdr->icmp6_dataun.echo.seq = 0;

	for (unsigned int i = 0; i < len - sizeof(struct icmp6_hdr); i++) {
		data[sizeof(struct icmp6_hdr) + i] = (uint8_t)i;
	}
}
#endif /* PSH_IPV6_SUPPORT */


static int ping_echo(int fd, uint8_t *data, int len)
{
	int ret;
	struct icmphdr *hdr = (struct icmphdr *)data;

	hdr->un.echo.sequence = htons(pingCommon.seq++);
	hdr->checksum = 0;
	hdr->checksum = htons(ping_chksum(data, len));

	ret = sendto(fd, data, len, 0, (struct sockaddr *)&pingCommon.raddr, sizeof(pingCommon.raddr));
	if (ret != len) {
		return -EIO;
	}

	return 0;
}


#ifdef PSH_IPV6_SUPPORT
static int ping_echo6(int fd, uint8_t *data, int len)
{
	int ret;
	struct icmp6_hdr *hdr = (struct icmp6_hdr *)data;

	hdr->icmp6_dataun.echo.seq = htons(pingCommon.seq++);

	/**
	 * Calculating checksum only for icmp message payload and saving locally.
	 * We use it to compare payload of ECHO REQUEST with ECHO REPLY.
	 * lwIP calculates msg checksum after selecting IPv6 src address and appends it to icmp msg hdr.
	 * */
	if (pingCommon.dataChksumRdy != 1) {
		pingCommon.dataChksum = ping_chksum(data + sizeof(struct icmp6_hdr), len - sizeof(struct icmp6_hdr));
		pingCommon.dataChksumRdy = 1;
	}

	ret = sendto(fd, data, len, 0, (struct sockaddr *)&pingCommon.raddr6, sizeof(pingCommon.raddr6));
	if (ret != len) {
		return -EIO;
	}

	return 0;
}
#endif /* PSH_IPV6_SUPPORT */


static int ping_reply(int fd, uint8_t *data, size_t len, char *addrstr, int addrlen)
{
	int ret;
	struct sockaddr_in rsin;
	socklen_t rlen = sizeof(rsin);
	struct icmphdr *icmphdr;
	int bytes;
	uint16_t rcvdChksum, calChksum;

	for (;;) {
		bytes = recvfrom(fd, data, len, 0, (struct sockaddr *)&rsin, &rlen);
		if (bytes <= 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				fprintf(stderr, "Host timeout\n");
			else
				fprintf(stderr, "ping: Fail to receive packet on socket!\n");
			return -EIO;
		}

		if (bytes < (int)(sizeof(struct iphdr) + sizeof(struct icmphdr))) {
			fprintf(stderr, "ping: Received msg too short (%d)!\n", bytes);
			return -EIO;
		}

		icmphdr = (struct icmphdr *)(data + sizeof(struct iphdr));
		bytes -= sizeof(struct iphdr);

		if ((icmphdr->un.echo.id == htons(pingCommon.myid)) && (icmphdr->type == ICMP_ECHOREPLY)) {
			break;
		}
	}

	if (inet_ntop(pingCommon.af, &rsin.sin_addr, addrstr, addrlen) == NULL) {
		fprintf(stderr, "ping: Invalid address received!\n");
		return -EFAULT;
	}

	ret = memcmp(&rsin.sin_addr, &pingCommon.raddr.sin_addr, sizeof(rsin.sin_addr));
	if (ret != 0) {
		fprintf(stderr, "ping: Response from invalid address: %s!\n", addrstr);
		return -EFAULT;
	}

	rcvdChksum = ntohs(icmphdr->checksum);
	icmphdr->checksum = 0;
	calChksum = ping_chksum((uint8_t *)icmphdr, bytes);
	if (rcvdChksum != calChksum) {
		fprintf(stderr, "ping: Response invalid checksum!\n");
		return -EIO;
	}

	if (ntohs(icmphdr->un.echo.sequence) != pingCommon.seq - 1) {
		fprintf(stderr, "ping: Response out of sequence (recv_seq=%u, expected_seq=%u)!\n", ntohs(icmphdr->un.echo.sequence), pingCommon.seq - 1);
		return -EIO;
	}

	return bytes;
}


#ifdef PSH_IPV6_SUPPORT
static int ping_reply6(int fd, uint8_t *data, size_t len, char *addrstr, int addrlen)
{
	int ret;
	ssize_t bytes;
	struct icmp6_hdr *icmp6hdr;
	struct sockaddr_in6 rsin6;
	socklen_t rsin6Len;
	uint16_t msgChksum;

	for (;;) {
		rsin6Len = sizeof(struct sockaddr_in6);
		bytes = recvfrom(fd, data, len, 0, (struct sockaddr *)&rsin6, &rsin6Len);
		if (bytes < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				fprintf(stderr, "ping: Echo timeout\n");
			}
			else {
				fprintf(stderr, "ping: Fail to receive packet on socket!\n");
			}

			return -EIO;
		}

		if (bytes < sizeof(struct icmp6_hdr)) {
			fprintf(stderr, "ping: Received msg too short (%ld)!\n", bytes);
			return -EIO;
		}

		ret = memcmp(&rsin6.sin6_addr, &pingCommon.raddr6.sin6_addr, sizeof(struct in6_addr));
		if (ret != 0) {
			continue;
		}

		icmp6hdr = (struct icmp6_hdr *)data;
		if (icmp6hdr->icmp6_type != ICMP6_ECHO_REPLY || icmp6hdr->icmp6_code != 0 ||
				icmp6hdr->icmp6_dataun.echo.id != htons(pingCommon.myid)) {
			/* Not our ICMPv6 packet, continue */
			continue;
		}

		if (icmp6hdr->icmp6_dataun.echo.seq != htons(pingCommon.seq - 1)) {
			fprintf(stderr, "ping: Response out of sequence (recv_seq=%u, expected_seq=%u)!\n",
					ntohs(icmp6hdr->icmp6_dataun.echo.seq), pingCommon.seq - 1);
			return -EIO;
		}

		msgChksum = ping_chksum(data + sizeof(struct icmp6_hdr), bytes - (int)sizeof(struct icmp6_hdr));
		if (msgChksum != pingCommon.dataChksum) {
			fprintf(stderr, "ping: Response invalid checksum!\n");
			return -EIO;
		}

		break;
	}

	if (inet_ntop(pingCommon.af, &rsin6.sin6_addr, addrstr, addrlen) == NULL) {
		fprintf(stderr, "ping: Invalid address received!\n");
		return -EFAULT;
	};

	return bytes;
}
#endif /* PSH_IPV6_SUPPORT */


static int ping_echoreply(int fd, uint8_t *req, uint8_t *resp)
{
	struct icmphdr *icmphdr;
	struct iphdr *iphdr;
	time_t elapsed;
	struct timespec ts1, ts2;
	char addrstr[INET_ADDRSTRLEN];
	char timestr[32];
	int bytes, ret;

	clock_gettime(CLOCK_MONOTONIC, &ts1);
	ret = ping_echo(fd, req, pingCommon.reqsz);
	if (ret < 0) {
		fprintf(stderr, "ping: Fail to send a packet!\n");
		return -EIO;
	}

	memset(resp, 0, pingCommon.respsz);
	bytes = ping_reply(fd, resp, pingCommon.respsz, addrstr, INET_ADDRSTRLEN);
	if (bytes < 0) {
		return -EIO;
	}

	clock_gettime(CLOCK_MONOTONIC, &ts2);
	elapsed = (ts2.tv_sec - ts1.tv_sec) * 1000000 + (ts2.tv_nsec - ts1.tv_nsec) / 1000;
	if ((elapsed % 1000) / 10 == 0) {
		snprintf(timestr, sizeof(timestr), "%llu ms", elapsed / 1000);
	}
	else {
		snprintf(timestr, sizeof(timestr), "%llu.%02llu ms", elapsed / 1000, (elapsed % 1000) / 10);
	}

	iphdr = (struct iphdr *)resp;
	icmphdr = (struct icmphdr *)(resp + sizeof(struct iphdr));

	printf("%d bytes received from %s: ttl=%u icmp_seq=%u time=%s\n",
			bytes, addrstr, iphdr->ttl, ntohs(icmphdr->un.echo.sequence), timestr);

	return 0;
}


#ifdef PSH_IPV6_SUPPORT
static int ping_echoreply6(int fd, uint8_t *req, uint8_t *resp)
{
	time_t elapsed;
	struct timespec ts1, ts2;
	struct icmp6_hdr *icmp6hdr;
	char addrstr[INET6_ADDRSTRLEN];
	char timestr[32];
	int bytes, ret;

	clock_gettime(CLOCK_MONOTONIC, &ts1);

	ret = ping_echo6(fd, req, pingCommon.reqsz);
	if (ret < 0) {
		fprintf(stderr, "ping: Fail to send a packet!\n");
		return -EIO;
	}

	memset(resp, 0, pingCommon.respsz);
	bytes = ping_reply6(fd, resp, pingCommon.respsz, addrstr, sizeof(addrstr));
	if (bytes < 0) {
		return -EIO;
	}

	clock_gettime(CLOCK_MONOTONIC, &ts2);
	elapsed = (ts2.tv_sec - ts1.tv_sec) * 1000000 + (ts2.tv_nsec - ts1.tv_nsec) / 1000;
	if ((elapsed % 1000) / 10 == 0) {
		snprintf(timestr, sizeof(timestr), "%llu ms", elapsed / 1000);
	}
	else {
		snprintf(timestr, sizeof(timestr), "%llu.%02llu ms", elapsed / 1000, (elapsed % 1000) / 10);
	}

	icmp6hdr = (struct icmp6_hdr *)resp;

	/* TODO: add hoplimit */
	printf("%d bytes received from %s icmp_seq=%u time=%s\n",
			bytes, addrstr, ntohs(icmp6hdr->icmp6_dataun.echo.seq), timestr);

	return 0;
}
#endif /* PSH_IPV6_SUPPORT */

static int ping_resolveAddress(int af, const char *src, void *dst)
{
	struct addrinfo hints = { 0 };
	struct addrinfo *result, *rp;
	int ret;

	hints.ai_family = af;
	hints.ai_socktype = SOCK_RAW;

	ret = getaddrinfo(src, NULL, &hints, &result);
	if (ret != 0) {
		return ret;
	}

	for (rp = result; rp != NULL; rp = rp->ai_next) {
		if (rp->ai_family != af) {
			continue;
		}

		if (rp->ai_family == AF_INET) {
			memcpy(dst, &((struct sockaddr_in *)rp->ai_addr)->sin_addr, sizeof(struct in_addr));
			break;
		}
		else if (rp->ai_family == AF_INET6) {
			memcpy(dst, &((struct sockaddr_in6 *)rp->ai_addr)->sin6_addr, sizeof(struct in6_addr));
			break;
		}
		else {
			/* required by MISRA C*/
		}
	}

	freeaddrinfo(result);

	return (rp == NULL) ? -EFAULT : 0;
}

static int ping_sendping4(const char *src)
{
	char addrstr[INET_ADDRSTRLEN];
	uint8_t *resp, *req;
	int fd;
	int ret = 0;

	pingCommon.af = AF_INET;
	pingCommon.raddr.sin_family = AF_INET;
	pingCommon.respsz = sizeof(struct iphdr) + pingCommon.reqsz;

	ret = ping_resolveAddress(pingCommon.af, src, &pingCommon.raddr.sin_addr);
	if (ret != 0) {
		fprintf(stderr, "ping: cannot resolve address: %s\n", src);
		return ret;
	}

	if (inet_ntop(pingCommon.af, &pingCommon.raddr.sin_addr, addrstr, sizeof(addrstr)) == NULL) {
		fprintf(stderr, "ping: Invalid address\n");
		return -errno;
	}

	req = malloc(pingCommon.reqsz);
	if (req == NULL) {
		fprintf(stderr, "ping: Out of memory!\n");
		return -ENOMEM;
	}

	resp = malloc(pingCommon.respsz);
	if (resp == NULL) {
		fprintf(stderr, "ping: Out of memory!\n");
		free(req);
		return -ENOMEM;
	}

	printf("PING %s (%.*s): %d data bytes\n",
			src, (int)sizeof(addrstr), addrstr, pingCommon.reqsz);

	fd = ping_sockconf();
	if (fd <= 0) {
		free(req);
		free(resp);
		return -EIO;
	}

	ping_reqinit(req, pingCommon.reqsz);

	while (pingCommon.cnt > 0 && psh_common.sigint == 0) {
		ret = ping_echoreply(fd, req, resp);
		if (ret != 0) {
			break;
		}

		pingCommon.cnt--;
		if (pingCommon.cnt > 0) {
			usleep(1000 * pingCommon.interval);
		}
	}

	close(fd);
	free(req);
	free(resp);

	return ret;
}

#ifdef PSH_IPV6_SUPPORT
static int ping_sendping6(const char *src)
{
	char addrstr[INET6_ADDRSTRLEN];
	uint8_t *resp, *req;
	int ret = 0;
	int fd;

	pingCommon.af = AF_INET6;
	pingCommon.raddr6.sin6_family = AF_INET6;
	pingCommon.respsz = pingCommon.reqsz;

	ret = ping_resolveAddress(pingCommon.af, src, &pingCommon.raddr6.sin6_addr);
	if (ret != 0) {
		fprintf(stderr, "ping: cannot resolve address: %s\n", src);
		return -EFAULT;
	}

	/* Determine destination address scope */
	if (IN6_IS_ADDR_LOOPBACK(&pingCommon.raddr6.sin6_addr)) {
		pingCommon.raddr6.sin6_scope_id = IPv6_ADDR_MC_SCOPE_NODELOCAL;
	}
	else if (IN6_IS_ADDR_LINKLOCAL(&pingCommon.raddr6.sin6_addr)) {
		pingCommon.raddr6.sin6_scope_id = IPv6_ADDR_MC_SCOPE_LINKLOCAL;
	}
	else if (IN6_IS_ADDR_ULA(&pingCommon.raddr6.sin6_addr)) {
		pingCommon.raddr6.sin6_scope_id = IPv6_ADDR_MC_SCOPE_ORGLOCAL;
	}
	else if (IN6_IS_ADDR_GLOBAL(&pingCommon.raddr6.sin6_addr)) {
		pingCommon.raddr6.sin6_scope_id = IPv6_ADDR_MC_SCOPE_GLOBAL;
	}
	else {
		/* Assign scope global if no match */
		pingCommon.raddr6.sin6_scope_id = IPv6_ADDR_MC_SCOPE_GLOBAL;
	}

	if (inet_ntop(pingCommon.af, &pingCommon.raddr6.sin6_addr, addrstr, sizeof(addrstr)) == NULL) {
		fprintf(stderr, "ping: Invalid address\n");
		return -EFAULT;
	}

	req = malloc(pingCommon.reqsz);
	if (req == NULL) {
		fprintf(stderr, "ping: Out of memory!\n");
		return -ENOMEM;
	}

	resp = malloc(pingCommon.respsz);
	if (resp == NULL) {
		fprintf(stderr, "ping: Out of memory!\n");
		free(req);
		return -ENOMEM;
	}

	printf("PING %s (%.*s): %d data bytes\n",
			src, (int)sizeof(addrstr), addrstr, pingCommon.reqsz);

	fd = ping_sockconf();
	if (fd <= 0) {
		free(req);
		free(resp);
		return -EIO;
	}

	ping_reqinit6(req, pingCommon.reqsz);

	while (pingCommon.cnt > 0 && psh_common.sigint == 0) {
		ret = ping_echoreply6(fd, req, resp);
		if (ret != 0) {
			break;
		}

		pingCommon.cnt--;
		if (pingCommon.cnt > 0) {
			usleep(1000 * pingCommon.interval);
		}
	}

	close(fd);
	free(req);
	free(resp);

	return ret;
}
#endif /* PSH_IPV6_SUPPORT */


int psh_ping(int argc, char **argv)
{
	char *end;
	int c;
	int ret = 0;

	pingCommon.cnt = 5;
	pingCommon.interval = 1000;
	pingCommon.timeout = 2000;
	pingCommon.seq = 1;
	pingCommon.ttl = 64;
	pingCommon.reqsz = 56 + max(sizeof(struct icmphdr), sizeof(struct icmp6_hdr));
	pingCommon.icmpProto = IPPROTO_ICMP;

	while ((c = getopt(argc, argv, "i:t:c:W:s:h6")) != -1) {
		switch (c) {
			case 'c':
				pingCommon.cnt = strtoul(optarg, &end, 10);
				if (*end != '\0' || pingCommon.cnt <= 0) {
					fprintf(stderr, "ping: Wrong count value!\n");
					return EXIT_FAILURE;
				}
				break;
			case 't':
				pingCommon.ttl = strtoul(optarg, &end, 10);
				if (*end != '\0' || pingCommon.ttl <= 0) {
					fprintf(stderr, "ping: Wrong ttl value!\n");
					return EXIT_FAILURE;
				}
				break;
			case 'i':
				pingCommon.interval = strtoul(optarg, &end, 10);
				if (*end != '\0' || pingCommon.interval < 0) {
					fprintf(stderr, "ping: Wrong interval value!\n");
					return EXIT_FAILURE;
				}
				break;
			case 'W':
				pingCommon.timeout = strtoul(optarg, &end, 10);
				if (*end != '\0' || pingCommon.timeout <= 100) {
					fprintf(stderr, "ping: Wrong timeout value!\n");
					return EXIT_FAILURE;
				}
				break;
			case 's':
				pingCommon.reqsz = strtoul(optarg, &end, 10) + sizeof(struct icmphdr);
				if (*end != '\0' || pingCommon.reqsz > (MAX_ECHO_PAYLOAD + sizeof(struct icmphdr))) {
					fprintf(stderr, "ping: Wrong payload len\n");
					return EXIT_FAILURE;
				}
				break;
#ifdef PSH_IPV6_SUPPORT
			case '6':
				pingCommon.icmpProto = IPPROTO_ICMPV6;
				pingCommon.dataChksumRdy = 0;
				break;
#endif
			default:
			case 'h':
				ping_help();
				return 0;
		}
	}

	if (argc - optind != 1) {
		fprintf(stderr, "ping: Expected address!\n");
		return EXIT_FAILURE;
	}

	if (pingCommon.icmpProto == IPPROTO_ICMP) {
		ret = ping_sendping4(argv[optind]);
#ifdef PSH_IPV6_SUPPORT
	}
	else {
		ret = ping_sendping6(argv[optind]);
#endif
	}

	return (ret >= 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}


void __attribute__((constructor)) ping_registerapp(void)
{
	static psh_appentry_t app = { .name = "ping", .run = psh_ping, .info = psh_pinginfo };
	psh_registerapp(&app);
}
