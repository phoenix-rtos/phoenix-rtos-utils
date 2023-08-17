/*
 * Phoenix-RTOS
 *
 * nc - TCP and UDP connect or listen
 *
 * Copyright 2021 Phoenix Systems
 * Author: Maciej Purski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include "../psh.h"


void psh_ncinfo(void)
{
	printf("TCP and UDP connections and listens");
}


static void psh_nc_help(void)
{
	printf("Usage: nc [options] [destination] [port]\n");
	printf("Options\n");
	printf("  -4:  IPv4 only\n");
	printf("  -6:  IPv6 only\n");
	printf("  -h:  prints help\n");
	printf("  -l:  listen mode, required port number\n");
	printf("  -p:  source port\n");
	printf("  -s:  source addr\n");
	printf("  -u:  datagram socket\n");
}


/*
 * This could be achieved using getaddrinfo(),
 * however it does not recognize scope id for now
 * and requires dynamic memory, which we
 * are trying to avoid.
 */
static int psh_nc_sockaddrFill(int *af, char *addrstr, char *portstr,
                               struct sockaddr_storage *sockaddr, socklen_t *addrlen)
{
	struct sockaddr_in6 *sin6;
	struct sockaddr_in *sin;
	char *ifstr, *endptr;
	int port = 0, scopeid = 0;
	uint32_t addr[4] = { 0 };

	if (*af == AF_UNSPEC) {
		if (addrstr && strchr(addrstr, ':') != NULL)
			*af = AF_INET6;
		else
			*af = AF_INET;
	}

	/* Try to resolve scope id, accepting strings: <ipv6_addr>%(<ifname>|<ifidx>) */
	if (addrstr != NULL && *af == AF_INET6) {
		ifstr = strtok(addrstr, "%");
		if ((ifstr = strtok(NULL, "%")) != NULL) {
			if (isalpha(*ifstr)) {
				/* No number, we've got netif name */
				if ((scopeid = if_nametoindex(ifstr)) == 0) {
					fprintf(stderr, "nc: No such interface: %s!\n", ifstr);
					return -1;
				}
			}
			else {
				/* We have an interface id */
				scopeid = strtoul(ifstr, &endptr, 10);
				if (*endptr != '\0' || scopeid == 0) {
					fprintf(stderr, "nc: Wrong interface number: %s!\n", ifstr);
					return -1;
				}
			}
		}
	}

	if (addrstr != NULL && inet_pton(*af, addrstr, addr) != 1) {
		fprintf(stderr, "nc: Can't parse address!\n");
		return -1;
	}

	if (portstr != NULL) {
		port = strtoul(portstr, &endptr, 10);
		if (*endptr != '\0' || port <= 0 || htons(port) > 0xFFFF) {
			fprintf(stderr, "nc: Cant parse port number!\n");
			return -1;
		}
	}

	memset(sockaddr, 0, sizeof(*sockaddr));

	sockaddr->ss_family = *af;

	if (*af == AF_INET) {
		sin = (struct sockaddr_in *) sockaddr;
		sin->sin_port = htons(port);
		sin->sin_addr.s_addr = addr[0];
		*addrlen = sizeof(struct sockaddr_in);
	}
	else {
		sin6 = (struct sockaddr_in6 *) sockaddr;
		sin6->sin6_port = htons(port);
		sin6->sin6_scope_id = scopeid;
		memcpy(&sin6->sin6_addr, addr, sizeof(addr));
		*addrlen = sizeof(struct sockaddr_in6);
	}

	return 0;
}


static int psh_nc_sockstreamListen(int sfd, int socktype, struct sockaddr *sockaddr, socklen_t *addrlen)
{
	char buf[64];
	fd_set fds;
	int c, cfd = -1;
	struct sockaddr_in sin;
	socklen_t raddrlen = sizeof(struct sockaddr_in);

	memset(&sin, 0, raddrlen);
	if (socktype == SOCK_STREAM && listen(sfd, 0) < 0) {
		fprintf(stderr, "nc: Fail to listen on a socket!\n");
		return -1;
	}

	while (1) {
		FD_ZERO(&fds);
		/* Since not all platforms handle signals,
		 * we need to be able to stop the program by
		 * writing EOF to stdin.
		 */
		FD_SET(STDIN_FILENO, &fds);
		FD_SET(sfd, &fds);

		if (select(sfd + 1, &fds, NULL, NULL, NULL) <= 0) {
			return -1;
		}
		else if (FD_ISSET(STDIN_FILENO, &fds)) {
			while ((c = getchar()) != EOF && c != '\n') { }
			if (c == EOF)
				return -1;
		}
		else {
			if (socktype == SOCK_STREAM) {
				if ((cfd = accept(sfd, sockaddr, NULL)) < 0) {
					fprintf(stderr, "nc: Fail to receive connection!\n");
					return -1;
				}
				return cfd;
			}
			else if (socktype == SOCK_DGRAM) {
				/* In UDP case we wait for a first msg,
				 * save its originator's address and connect
				 * it.
				 */
				memset(buf, 0, sizeof(buf));
				*addrlen = sizeof(struct sockaddr_in6);
				if ((c = recvfrom(sfd, buf, sizeof(buf), 0, (struct sockaddr *) &sin, &raddrlen)) < 0) {
					fprintf(stderr, "nc: Can't receive msg!\n");
					return -1;
				}
				(void)psh_write(STDOUT_FILENO, buf, c);

				if ((connect(sfd, (struct sockaddr *) &sin, raddrlen)) < 0) {
					fprintf(stderr, "nc: Fail to connect!\n");
					return -1;
				}

				return sfd;
			}
			else {
				return -1;
			}
		}
	}
}


static void psh_nc_socktalk(int fd)
{
	fd_set fds;
	char buf[64];
	int n;

	while (1) {
		FD_ZERO(&fds);
		FD_SET(STDIN_FILENO, &fds);
		FD_SET(fd, &fds);

		if (select(fd + 1, &fds, NULL, &fds, NULL) <= 0)
			return;

		if (FD_ISSET(STDIN_FILENO, &fds)) {
			if ((n = read(STDIN_FILENO, buf, sizeof(buf))) <= 0)
				return;

			if (send(fd, buf, n, 0) <= 0)
				return;
			memset(buf, 0, n);
		}

		if (FD_ISSET(fd, &fds)) {
			if ((n = recv(fd, buf, sizeof(buf), 0)) <= 0)
				return;
			(void)psh_write(STDOUT_FILENO, buf, n);
			memset(buf, 0, n);
		}
	}
}


int psh_nc(int argc, char **argv)
{
	struct sockaddr_storage srcsockaddr, dstsockaddr;
	int c, lmode = 0;
	int socktype = SOCK_STREAM;
	char *srcaddr = NULL, *srcport = NULL, *dstaddr = NULL, *dstport = NULL;
	int af = AF_UNSPEC;
	int fd, cfd;
	socklen_t addrlen;

	while ((c = getopt(argc, argv, "hlu46s:p:")) != -1) {
		switch (c) {
		case 'l':
			lmode = 1;
			break;
		case 'u':
			socktype = SOCK_DGRAM;
			break;
		case '6':
			af = AF_INET6;
			break;
		case '4':
			af = AF_INET;
			break;
		case 's':
			srcaddr = optarg;
			break;
		case 'p':
			srcport = optarg;
			break;
		case 'h':
		default:
			psh_nc_help();
			return 0;
		}
	}

	argc -= optind;
	argv += optind;

	if (lmode) {
		switch (argc) {
		case 2:
			if (srcaddr != NULL || srcport != NULL) {
				fprintf(stderr, "nc: Too many arguments!\n");
				return -EINVAL;
			}
			srcaddr = *argv;
			srcport = *(argv + 1);
			break;
		case 1:
			if (srcport == NULL) {
				srcport = *argv;
			}
			else if (srcaddr == NULL) {
				srcaddr = *argv;
			}
			else {
				fprintf(stderr, "nc: Too many arguments!\n");
				return -EINVAL;
			}
			break;
		default:
			fprintf(stderr, "nc: Unexpected number of arguments!\n");
			return -EINVAL;
		}
	}
	else {
		if (argc < 2) {
			fprintf(stderr, "nc: Expected an address and a port number!\n");
			return -EINVAL;
		}

		dstaddr = *argv;
		dstport = *(argv + 1);

		if (psh_nc_sockaddrFill(&af, dstaddr, dstport, &dstsockaddr, &addrlen) < 0)
			return -EINVAL;
	}

	/* Optional for client and required for server */
	if (srcport != NULL || srcaddr != NULL) {
		if (psh_nc_sockaddrFill(&af, srcaddr, srcport, &srcsockaddr, &addrlen) < 0)
			return -EINVAL;
	}

	if ((fd = socket(af, socktype, socktype == SOCK_STREAM ? IPPROTO_TCP : IPPROTO_UDP)) < 0) {
		fprintf(stderr, "nc: Can't create a socket!\n");
		return -EIO;
	}

	if (srcport != NULL || srcaddr != NULL) {
		if (bind(fd, (struct sockaddr *) &srcsockaddr, addrlen) < 0) {
			fprintf(stderr, "nc: Can't bind to a socket!\n");
			close(fd);
			return -EIO;
		}
	}

	if (lmode) {
		if ((cfd = psh_nc_sockstreamListen(fd, socktype, (struct sockaddr *) &srcsockaddr, &addrlen)) < 0) {
			close(fd);
			return -EIO;
		}

		psh_nc_socktalk(cfd);
		if (cfd != fd)
			close(cfd);
	}
	else {
		if (connect(fd, (struct sockaddr *) &dstsockaddr, addrlen) < 0) {
			fprintf(stderr, "nc: Can't connect to remote!\n");
			close(fd);
			return -EIO;
		}

		psh_nc_socktalk(fd);
	}

	close(fd);

	return 0;
}


void __attribute__((constructor)) nc_registerapp(void)
{
	static psh_appentry_t app = {.name = "nc", .run = psh_nc, .info = psh_ncinfo};
	psh_registerapp(&app);
}
