/*
 * Phoenix-RTOS
 *
 * ifconfig - configure network interfaces
 *
 * Copyright 2023 Phoenix Systems
 * Author: Andrzej Stalke
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <inttypes.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <ifaddrs.h>

#include "../psh.h"

#define IFCONFIG_VERBOSE 1
#define IFCONFIG_ALL     2

static void psh_ifconfigInfo(void)
{
	printf("configures network interfaces");
}


static inline const char *psh_ifconfigPrintFlag(unsigned int flag)
{
	switch (flag) {
		case IFF_UP:
			return "UP";
		case IFF_BROADCAST:
			return "BROADCAST";
		case IFF_DEBUG:
			return "DEBUG";
		case IFF_LOOPBACK:
			return "LOOPBACK";
		case IFF_POINTOPOINT:
			return "POINTOPOINT";
		case IFF_NOTRAILERS:
			return "NOTRAILERS";
		case IFF_RUNNING:
			return "RUNNING";
		case IFF_NOARP:
			return "NOARP";
		case IFF_PROMISC:
			return "PROMISC";
		case IFF_ALLMULTI:
			return "ALLMULTI";
		case IFF_OACTIVE:
			return "OACTIVE";
		case IFF_SIMPLEX:
			return "SIMPLEX";
		case IFF_LINK0:
			return "LINK0";
		case IFF_LINK1:
			return "LINK1";
		case IFF_MULTICAST:
			return "MULTICAST";
		case IFF_DYNAMIC:
			return "DYNAMIC";
		default:
			return "UNKNOWN_FLAG";
	}
}


static inline int psh_ifconfigPrintInterface(const struct ifaddrs *interface, int sd)
{
	unsigned int flags, i;
	struct in_addr addr, broadcast, mask;
	int ret;
	struct ifreq ioctlInterface;

	(void)memcpy(ioctlInterface.ifr_name, interface->ifa_name, strlen(interface->ifa_name));
	/* Get interface flags */
	ret = ioctl(sd, SIOCGIFFLAGS, &ioctlInterface);
	if (ret < 0) {
		perror("ioctl(SIOCGIFFLAGS)");
		return ret;
	}
	flags = ioctlInterface.ifr_flags;

	/* Get interface IP address */
	ret = ioctl(sd, SIOCGIFADDR, &ioctlInterface);
	if (ret < 0) {
		perror("ioctl(SIOCGIFADDR)");
		return ret;
	}
	addr = ((struct sockaddr_in *)&ioctlInterface.ifr_addr)->sin_addr;

	/* Get interface broadcast IP address */
	ret = ioctl(sd, SIOCGIFBRDADDR, &ioctlInterface);
	if (ret < 0) {
		perror("ioctl(SIOCGIFBRDADDR)");
		return ret;
	}
	broadcast = ((struct sockaddr_in *)&ioctlInterface.ifr_addr)->sin_addr;

	/* Get interface IP mask */
	ret = ioctl(sd, SIOCGIFNETMASK, &ioctlInterface);
	if (ret < 0) {
		perror("ioctl(SIOCGIFNETMASK)");
		return ret;
	}
	mask = ((struct sockaddr_in *)&ioctlInterface.ifr_addr)->sin_addr;

	printf("%-10s", interface->ifa_name);

	if ((flags & IFF_LOOPBACK) == 0) {
		/* Get interface hardware address */
		(void)memset(&ioctlInterface.ifr_hwaddr, 0, sizeof(struct sockaddr));
		ret = ioctl(sd, SIOCGIFHWADDR, &ioctlInterface);
		if (ret < 0) {
			perror("ioctl(SIOCGIFHWADDR)");
			return ret;
		}
		printf("HWAddr");
		for (ret = 0; ret < 6; ++ret) {
			printf(":%02hhx", ioctlInterface.ifr_hwaddr.sa_data[ret]);
		}
		printf("\n%10s", "");
	}

	printf("inet addr:%s ", inet_ntoa(addr));
	printf("Broadcast:%s ", inet_ntoa(broadcast));
	printf("Mask:%s\n", inet_ntoa(mask));
	if (flags != 0) {
		printf("%10s", "");
		for (i = 1; i <= flags; i <<= 1) {
			if ((flags & i) != 0) {
				printf("%s ", psh_ifconfigPrintFlag(i));
			}
		}
	}

	/* Get MTU */
	ret = ioctl(sd, SIOCGIFMTU, &ioctlInterface);
	if (ret < 0) {
		perror("ioctl(SIOCGIFMTU)");
		return ret;
	}
	printf("MTU:%d", ioctlInterface.ifr_mtu);
	/* Get metric */
	ret = ioctl(sd, SIOCGIFMETRIC, &ioctlInterface);
	if (ret < 0) {
		perror("ioctl(SIOCGIFMETRIC)");
		return ret;
	}
	printf(" Metric:%d", (ioctlInterface.ifr_metric != 0) ? ioctlInterface.ifr_metric : 1);
	puts("\n");
	return ret;
}


static int psh_ifconfigDisplay(unsigned int flags, const char *interfaceName)
{
	struct ifaddrs *interface;
	int ret, sd;

	ret = getifaddrs(&interface);
	if (ret != 0) {
		perror("getifaddrs");
		return ret;
	}

	sd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sd < 0) {
		perror("socket");
		return sd;
	}

	for (; interface != NULL; interface = interface->ifa_next) {
		if ((interfaceName == NULL) || (strcmp(interfaceName, interface->ifa_name) == 0)) {
			if (((flags & IFCONFIG_ALL) != 0) || ((interface->ifa_flags & IFF_UP) != 0)) {
				ret = psh_ifconfigPrintInterface(interface, sd);
				if (ret < 0) {
					break;
				}
			}
		}
	}
	close(sd);
	freeifaddrs(interface);
	return ret;
}


static int psh_ifconfig(int argc, char **argv)
{
	const char *interfaceName = NULL;
	unsigned int flags = 0;
	int opt, ret;

	while ((opt = getopt(argc, argv, "va")) != -1) {
		switch (opt) {
			case 'v':
				flags |= IFCONFIG_VERBOSE;
				break;
			case 'a':
				flags |= IFCONFIG_ALL;
				break;
			default:
				break;
		}
	}
	opt = optind;
	if (opt < argc) {
		interfaceName = argv[opt];
		opt += 1;
	}

	if ((opt >= argc) || ((flags & IFCONFIG_ALL) != 0)) {
		/* Ignore options, only display */
		ret = psh_ifconfigDisplay(flags, interfaceName);
		if (ret != 0) {
			fprintf(stderr, "%s: error fetching interface information: %d", (interfaceName == NULL) ? "" : interfaceName, ret);
			return EXIT_FAILURE;
		}
	}
	else {
		/* Read options */
		// TODO
		for (; opt < argc; opt++) {
			printf("Non-option argument %s\n", argv[opt]);
		}
	}

	return EXIT_SUCCESS;
}


void __attribute__((constructor)) ifconfig_registerapp(void)
{
	static psh_appentry_t app = { .name = "ifconfig", .run = psh_ifconfig, .info = psh_ifconfigInfo };
	psh_registerapp(&app);
}
