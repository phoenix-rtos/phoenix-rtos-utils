/*
 * Phoenix-RTOS
 *
 * ntpclient - set the system's date from a remote host
 *
 * Copyright 2022 Phoenix Systems
 * Author: Gerard Swiderski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <errno.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <unistd.h>

#include "../psh.h"


#define NTP_JAN1970_DELTA 2208988800ull

/* Convert fraction (divide by 4294.967296) to uSec */
#define FRAC_TO_USEC(x) (((x) >> 12) - 759 * ((((x) >> 10) + 32768) >> 16))

#define NTP_LI_VN_MODE(li, vn, mode) (((((uint8_t)li) & 3) << 6) | ((((uint8_t)vn) & 7) << 3) | ((((uint8_t)mode) & 7) << 0))
#define NTP_VERSION(li_vn_mode)      ((uint8_t)(((li_vn_mode) >> 3) & 7))
#define NTP_LEAP(li_vn_mode)         ((uint8_t)(((li_vn_mode) >> 6) & 3))
#define NTP_LEAP_NOSYNC              0

#define NTP_MODE(li_vn_mode) ((uint8_t)(((li_vn_mode) >> 0) & 7))
#define NTP_MODE_PASSIVE     2
#define NTP_MODE_CLIENT      3
#define NTP_MODE_SERVER      4


struct sntp_pkt_s {
	uint8_t li_vn_mode;      /* Leap indicator, version, mode */
	uint8_t stratum;         /* Stratum level of the local clock. */
	uint8_t poll;            /* Maximum interval between successive messages. */
	int8_t precision;        /* Precision of the local clock. */
	uint32_t rootDelay;      /* Total round trip delay time. */
	uint32_t rootDispersion; /* Max error aloud from primary clock source. */
	uint32_t refId;          /* Reference clock identifier. */
	uint32_t refTm_sec;      /* Reference time-stamp seconds. */
	uint32_t refTm_frac;     /* Reference time-stamp fraction of a second. */
	uint32_t origTm_sec;     /* Originate time-stamp seconds. */
	uint32_t origTm_frac;    /* Originate time-stamp fraction of a second. */
	uint32_t rxTm_sec;       /* Received time-stamp seconds. */
	uint32_t rxTm_frac;      /* Received time-stamp fraction of a second. */
	uint32_t txTm_sec;       /* Transmit time-stamp seconds. */
	uint32_t txTm_frac;      /* Transmit time-stamp fraction of a second. */
} __attribute__((packed));


static int doError(const char *fName, int err)
{
	fprintf(stderr, "netclient: %s() failed, err=%d\n", fName, err);
	return err;
}


static uint32_t *aiToAddr(struct addrinfo *ai)
{
	return &(((struct sockaddr_in *)ai->ai_addr)->sin_addr.s_addr);
}


static int ntpclient_connect(const char *host)
{
	int ret = EOK, sockfd;
	struct addrinfo *res;
	struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_DGRAM, .ai_protocol = IPPROTO_UDP };
	char hostaddr[INET_ADDRSTRLEN];

	if (host == NULL) {
		return doError("ntpclient_connect", -EINVAL);
	}

	printf("Using NTP server: %s\n", host);

	ret = getaddrinfo(host, "123", &hints, &res);
	if (ret != 0) {
		if (ret == EAI_SYSTEM) {
			ret = -errno;
		}
		return doError("getaddrinfo", ret);
	}

	do {
		if (inet_ntop(res->ai_family, aiToAddr(res), hostaddr, sizeof(hostaddr)) == NULL) {
			ret = doError("inet_ntop", -errno);
			break;
		}

		sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
		if (sockfd < 0) {
			ret = doError("socket", -errno);
			break;
		}

		if (connect(sockfd, res->ai_addr, res->ai_addrlen) < 0) {
			ret = doError("connect", -errno);
			close(sockfd);
			break;
		}

		ret = sockfd;
	} while (0);

	freeaddrinfo(res);

	return ret;
}


static int ntpclient_gettimepacket(int sockfd, struct sntp_pkt_s *pkt)
{
	size_t len;
	uint8_t *ptr;

	memset(pkt, 0, sizeof(*pkt));

	pkt->li_vn_mode = NTP_LI_VN_MODE(NTP_LEAP_NOSYNC, 4, NTP_MODE_CLIENT);
	pkt->stratum = 16; /* use unspecified stratum, as we're out of sync */
	pkt->poll = 3;
	pkt->precision = -6;

	len = sizeof(*pkt);
	ptr = (uint8_t *)pkt;
	while (len > 0) {
		ssize_t bytes = write(sockfd, ptr, len);
		if (bytes < 0) {
			if (errno != EAGAIN && errno != EINTR) {
				return doError("write", -errno);
			}
			continue;
		}
		len -= bytes;
		ptr += bytes;
	}

	len = sizeof(*pkt);
	ptr = (uint8_t *)pkt;
	while (len > 0) {
		ssize_t bytes = read(sockfd, ptr, len);
		if (bytes < 0) {
			if (errno != EAGAIN && errno != EINTR) {
				return doError("read", -errno);
			}
			continue;
		}
		len -= bytes;
		ptr += bytes;
	}

	if ((NTP_MODE(pkt->li_vn_mode) != NTP_MODE_SERVER && NTP_MODE(pkt->li_vn_mode) != NTP_MODE_PASSIVE) || pkt->stratum >= 16) {
		return doError("ntpclient_gettimepacket", -EPROTO);
	}

	return EOK;
}


static int ntpclient_settime(struct sntp_pkt_s *pkt)
{
	struct timeval tv_new, tv_old;

	if (gettimeofday(&tv_old, NULL) < 0) {
		return doError("gettimeofday", -errno);
	}

	pkt->txTm_sec = ntohl(pkt->txTm_sec);
	pkt->txTm_frac = ntohl(pkt->txTm_frac);

	tv_new.tv_sec = pkt->txTm_sec - NTP_JAN1970_DELTA;
	tv_new.tv_usec = FRAC_TO_USEC(pkt->txTm_frac);

	if (settimeofday(&tv_new, NULL) < 0) {
		return doError("settimeofday", -errno);
	}

	printf("System time in UTC was %s", ctime(&tv_old.tv_sec));
	printf("System time set to UTC %s", ctime(&tv_new.tv_sec));

	return EOK;
}


static void psh_ntpclientInfo(void)
{
	printf("set the system's date from a remote host");
}

static void psh_ntpclientUsage(void)
{
	printf("Usage: ntpclient [options]\n"
		   "  -h:  prints help\n"
		   "  -s:  specify ntp server address\n");
}


static int psh_ntpclientMain(int argc, char **argv)
{
	int opt, sockfd;
	struct sntp_pkt_s pkt;
	const char *ntp_host = "pool.ntp.org";

	while ((opt = getopt(argc, argv, "s:h")) != -1) {
		switch (opt) {
			case 's':
				ntp_host = optarg;
				break;

			default:
				/* fall-through */
			case 'h':
				psh_ntpclientUsage();
				return EXIT_SUCCESS;
		}
	}

	sockfd = ntpclient_connect(ntp_host);
	if (sockfd < 0) {
		return EXIT_FAILURE;
	}

	if (ntpclient_gettimepacket(sockfd, &pkt) < 0) {
		close(sockfd);
		return EXIT_FAILURE;
	}

	close(sockfd);

	if (ntpclient_settime(&pkt) < 0) {
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}


void __attribute__((constructor)) ntpclient_registerapp(void)
{
	static psh_appentry_t app = { .name = "ntpclient", .run = psh_ntpclientMain, .info = psh_ntpclientInfo };
	psh_registerapp(&app);
}
