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

enum operation {operation_setFlag, operation_unsetFlag, operation_toggleFlag};

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

	(void)strncpy(ioctlInterface.ifr_name, interface->ifa_name, IFNAMSIZ - 1);
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


static inline int psh_ifconfigDisplay(unsigned int flags, const char *interfaceName, int sd)
{
	struct ifaddrs *interface;
	int ret;

	ret = getifaddrs(&interface);
	if (ret != 0) {
		perror("getifaddrs");
		return ret;
	}

	for (; interface != NULL; interface = interface->ifa_next) {
		if ((interfaceName == NULL) || (strcmp(interfaceName, interface->ifa_name) == 0)) {
			if (((flags & IFCONFIG_ALL) != 0) || (((interface->ifa_flags & IFF_UP) != 0)) || (interfaceName != NULL)) {
				ret = psh_ifconfigPrintInterface(interface, sd);
				if (ret < 0) {
					break;
				}
			}
		}
	}
	freeifaddrs(interface);
	return ret;
}


static int psh_ifconfigChangeFlag(struct ifreq *ioctlInterface, int sd, unsigned int flag, enum operation op)
{
	int ret;
	ret = ioctl(sd, SIOCGIFFLAGS, ioctlInterface);
	if (ret != 0) {
		perror("ioctl(SIOCGIFFLAGS)");
		return ret;
	}
	switch (op) {
		case operation_setFlag:
			ioctlInterface->ifr_flags |= flag;
			break;
		case operation_unsetFlag:
			ioctlInterface->ifr_flags &= ~flag;
			break;
		case operation_toggleFlag:
			ioctlInterface->ifr_flags ^= flag;
			break;
		default:
			/* Programming error */
			return -1;
	}

	ret = ioctl(sd, SIOCSIFFLAGS, ioctlInterface);
	if (ret != 0) {
		perror("ioctl(SIOCSIFFLAGS)");
		return ret;
	}
	return 0;
}


static int psh_ifconfigUpHandler(struct ifreq *ioctlInterface, int sd, int argc, char **argv, int *opt)
{
	(void)argc;
	(void)argv;
	(void)opt;
	return psh_ifconfigChangeFlag(ioctlInterface, sd, IFF_UP, operation_setFlag);
}


static int psh_ifconfigDownHandler(struct ifreq *ioctlInterface, int sd, int argc, char **argv, int *opt)
{
	(void)argc;
	(void)argv;
	(void)opt;
	return psh_ifconfigChangeFlag(ioctlInterface, sd, IFF_UP, operation_unsetFlag);
}

static int psh_ifconfigNetmask(struct ifreq *ioctlInterface, int sd, int argc, char **argv, int *opt)
{
	int ret;

	if (*opt >= argc) {
		/* Not enough arguments */
		fprintf(stderr, "Not enough arguments\n");
		return -1;
	}

	(*opt) += 1;
	((struct sockaddr_in *)&ioctlInterface->ifr_netmask)->sin_family = AF_INET;
	ret = inet_aton(argv[*opt], &((struct sockaddr_in *)&ioctlInterface->ifr_netmask)->sin_addr);
	if (ret == 0) {
		fprintf(stderr, "Invalid netmask value\n");
		return -1;
	}
	ret = ioctl(sd, SIOCSIFNETMASK, ioctlInterface);
	if (ret != 0) {
		perror("ioctl(SIOCSIFNETMASK)");
		return ret;
	}
	return 0;
}


static int psh_ifconfigBroadcast(struct ifreq *ioctlInterface, int sd, int argc, char **argv, int *opt)
{
	int ret;

	if (*opt >= argc) {
		fprintf(stderr, "Not enough arguments\n");
		return -1;
	}

	(*opt) += 1;
	((struct sockaddr_in *)&ioctlInterface->ifr_broadaddr)->sin_family = AF_INET;
	ret = inet_aton(argv[*opt], &((struct sockaddr_in *)&ioctlInterface->ifr_broadaddr)->sin_addr);
	if (ret == 0) {
		fprintf(stderr, "Invalid broadcast value\n");
		return -1;
	}
	ret = ioctl(sd, SIOCSIFBRDADDR, ioctlInterface);
	if (ret != 0) {
		perror("ioctl(SIOCSIFBRDADDR)");
		return ret;
	}
	return 0;
}


static int psh_ifconfigMTU(struct ifreq *ioctlInterface, int sd, int argc, char **argv, int *opt)
{
	int ret;
	char *endptr;

	if (*opt >= argc) {
		fprintf(stderr, "Not enough arguments\n");
		return -1;
	}

	(*opt) += 1;
	ioctlInterface->ifr_mtu = strtol(argv[*opt], &endptr, 0);
	if (*endptr != '\0') {
		fprintf(stderr, "Invalid MTU value\n");
		return -1;
	}
	ret = ioctl(sd, SIOCSIFMTU, ioctlInterface);
	if (ret != 0) {
		perror("ioctl(SIOCSIFMTU)");
		return ret;
	}
	return 0;
}


static int psh_ifconfigPointToPoint(struct ifreq *ioctlInterface, int sd, int argc, char **argv, int *opt)
{
	int ret;

	if (*opt >= argc) {
		fprintf(stderr, "Not enough arguments\n");
		return -1;
	}

	(*opt) += 1;
	((struct sockaddr_in *)&ioctlInterface->ifr_dstaddr)->sin_family = AF_INET;
	ret = inet_aton(argv[*opt], &((struct sockaddr_in *)&ioctlInterface->ifr_dstaddr)->sin_addr);
	if (ret == 0) {
		fprintf(stderr, "Invalid point-to-point address value\n");
		return -1;
	}
	ret = ioctl(sd, SIOCSIFDSTADDR, ioctlInterface);
	if (ret != 0) {
		perror("ioctl(SIOCSIFDSTADDR)");
		return ret;
	}
	return psh_ifconfigChangeFlag(ioctlInterface, sd, IFF_POINTOPOINT, operation_setFlag);
}


static int psh_ifconfigMulticast(struct ifreq *ioctlInterface, int sd, int argc, char **argv, int *opt)
{
	(void)argc;
	(void)argv;
	(void)opt;
	return psh_ifconfigChangeFlag(ioctlInterface, sd, IFF_MULTICAST, operation_toggleFlag);
}


static int psh_ifconfigAllmulti(struct ifreq *ioctlInterface, int sd, int argc, char **argv, int *opt)
{
	(void)argc;
	(void)argv;
	(void)opt;
	return psh_ifconfigChangeFlag(ioctlInterface, sd, IFF_ALLMULTI, operation_toggleFlag);
}


static int psh_ifconfigPromisc(struct ifreq *ioctlInterface, int sd, int argc, char **argv, int *opt)
{
	(void)argc;
	(void)argv;
	(void)opt;
	return psh_ifconfigChangeFlag(ioctlInterface, sd, IFF_PROMISC, operation_toggleFlag);
}


static int psh_ifconfigArp(struct ifreq *ioctlInterface, int sd, int argc, char **argv, int *opt)
{
	(void)argc;
	(void)argv;
	(void)opt;
	return psh_ifconfigChangeFlag(ioctlInterface, sd, IFF_NOARP, operation_toggleFlag);
}


static const struct {
	const char *name;
	int (*handler)(struct ifreq *ioctlInterface, int sd, int argc, char **argv, int *opt);
} psh_ifconfigArguments[] = {
	{ .name = "up", .handler = psh_ifconfigUpHandler },
	{ .name = "down", .handler = psh_ifconfigDownHandler },
	{ .name = "netmask", .handler = psh_ifconfigNetmask },
	{ .name = "broadcast", .handler = psh_ifconfigBroadcast },
	{ .name = "-broadcast", .handler = psh_ifconfigBroadcast },
	{ .name = "mtu", .handler = psh_ifconfigMTU },
	{ .name = "dstaddr", .handler = psh_ifconfigPointToPoint },
	{ .name = "pointopoint", .handler = psh_ifconfigPointToPoint },
	{ .name = "-pointopoint", .handler = psh_ifconfigPointToPoint },
	{ .name = "multicast", .handler = psh_ifconfigMulticast },
	{ .name = "allmulti", .handler = psh_ifconfigAllmulti },
	{ .name = "-allmulti", .handler = psh_ifconfigAllmulti },
	{ .name = "promisc", .handler = psh_ifconfigPromisc },
	{ .name = "-promisc", .handler = psh_ifconfigPromisc },
	{ .name = "arp", .handler = psh_ifconfigArp },
	{ .name = "-arp", .handler = psh_ifconfigArp },
};


static inline int psh_ifconfigHandleArguments(const char *interfaceName, int argc, char **argv, int opt, int sd)
{
	int ret, found;
	size_t i;
	struct ifreq ioctlInterface;
	const char *argumentName;

	for (; opt < argc; opt++) {
		(void)strncpy(ioctlInterface.ifr_name, interfaceName, IFNAMSIZ - 1);
		(void)memset(&ioctlInterface.ifr_ifru, 0, sizeof(ioctlInterface.ifr_ifru));
		argumentName = argv[opt];
		ret = 1;
		found = 0;
		for (i = 0; i < sizeof(psh_ifconfigArguments) / sizeof(*psh_ifconfigArguments); ++i) {
			if (strcmp(argumentName, psh_ifconfigArguments[i].name) == 0) {
				found = 1;
				ret = psh_ifconfigArguments[i].handler(&ioctlInterface, sd, argc, argv, &opt);
				break;
			}
		}
		if (found == 0) {
			(void)strncpy(ioctlInterface.ifr_name, interfaceName, IFNAMSIZ - 1);
			(void)memset(&ioctlInterface.ifr_ifru, 0, sizeof(ioctlInterface.ifr_ifru));
			/* Is this an IP address? */
			((struct sockaddr_in *)&ioctlInterface.ifr_netmask)->sin_family = AF_INET;
			ret = inet_aton(argumentName, &((struct sockaddr_in *)&ioctlInterface.ifr_netmask)->sin_addr);
			if (ret == 0) {
				fprintf(stderr, "Unknown argument: %s\n", argumentName);
				ret = 1;
			}
			else {
				ret = ioctl(sd, SIOCSIFADDR, &ioctlInterface);
				if (ret < 0) {
					perror("ioctl(SIOCGIFFLAGS)");
				}
			}
		}
		if (ret != 0) {
			break;
		}
	}
	return ret;
}


static int psh_ifconfig(int argc, char **argv)
{
	const char *interfaceName = NULL;
	unsigned int flags = 0;
	int opt, ret, sd;

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

	sd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sd < 0) {
		perror("socket");
		return EXIT_FAILURE;
	}

	if ((opt >= argc) || ((flags & IFCONFIG_ALL) != 0)) {
		/* Ignore options, only display */
		ret = psh_ifconfigDisplay(flags, interfaceName, sd);
		if (ret != 0) {
			fprintf(stderr, "%s: error fetching interface information: %d\n", (interfaceName == NULL) ? "" : interfaceName, ret);
			ret = EXIT_FAILURE;
		}
	}
	else {
		if (strcmp(argv[opt], "inet") == 0) {
			opt += 1;
		}
		ret = psh_ifconfigHandleArguments(interfaceName, argc, argv, opt, sd);
		if (ret != 0) {
			ret = EXIT_FAILURE;
		}
	}

	close(sd);
	return ret;
}


void __attribute__((constructor)) ifconfig_registerapp(void)
{
	static psh_appentry_t app = { .name = "ifconfig", .run = psh_ifconfig, .info = psh_ifconfigInfo };
	psh_registerapp(&app);
}
