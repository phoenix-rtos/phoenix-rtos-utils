/*
 * Phoenix-RTOS
 *
 * route - manages routing tables
 *
 * Copyright 2023 Phoenix Systems
 * Author: Gerard Swiderski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <net/if.h>

#include "../psh.h"


#define OPT_NUMERIC 1

#define SETFL_ADDR    1
#define SETFL_MASK    2
#define SETFL_GATEWAY 4
#define SETFL_DEVICE  8
#define SETFL_METRIC  16
#define SETFL_REJECT  32


/* clang-format off */

enum { act_none, act_show, act_add, act_del };

/* clang-format on */


void psh_routeUsage(void)
{
	fprintf(stderr,
		"Usage: route [options] [<add|del> <route decl>]\n"
		"<route decl>: [-net|-host] target [netmask Mask] [gw Addr] [metric Num] <[dev] name]>\n"
		"target, Mask, Addr format is IP; Num is numeric value; and name of net interface\n"
		"Options:\n"
		" -n  don't resolve names\n"
		" -h  help\n");
}


static void psh_routeInfo(void)
{
	printf("prints the name of current working directory");
}


static void setFlags(char *str, int flags)
{
	static const char sFlags[] = "UGHRDM!";
	static const uint32_t vFlags[] = {
		RTF_UP, RTF_GATEWAY, RTF_HOST, RTF_REINSTATE, RTF_DYNAMIC, RTF_MODIFIED, RTF_REJECT
	};

	for (int i = 0; sFlags[i] != '\0'; i++) {
		if (((uint16_t)flags & vFlags[i]) != 0) {
			*str++ = sFlags[i];
		}
	}
	*str = '\0';
}


static int psh_routeShow(int options)
{
	const char *path = "/dev/route";
	FILE *f = fopen(path, "r");
	if (f == NULL) {
		printf("Error opening %s", path);
		return 1;
	}

	char line[256];
	if (fgets(line, sizeof(line), f) == NULL) {
		fclose(f);
		return -1;
	}

	printf("%-15s %-15s %-15s %-5s %-6s %-6s %3s %s\n", "Destination", "Gateway", "Genmask", "Flags", "Metric", "Ref", "Use", "Iface");

	for (;;) {
		char iface[IFNAMSIZ];
		char sFlags[7];
		int flags;
		int refcnt, use;
		int metric;
		int mtu, win, irtt;
		struct in_addr dst;
		struct in_addr gw;
		struct in_addr mask;

		int res = fscanf(f, "%10s%x%x%x%d%d%d%x%d%d%d\n", iface, &dst.s_addr, &gw.s_addr, &flags, &refcnt, &use, &metric, &mask.s_addr, &mtu, &win, &irtt);
		if (res != 11) {
			break;
		}

		printf("%-15s ", (((options & OPT_NUMERIC) == 0) && (dst.s_addr == 0) && (mask.s_addr == 0)) ? "default" : inet_ntoa(dst));
		printf("%-15s ", inet_ntoa(gw));
		printf("%-15s ", inet_ntoa(mask));
		setFlags(sFlags, flags);

		printf("%-5s %-6d %-6d %3d %s\n", sFlags, metric, refcnt, use, iface);
	}

	fclose(f);

	return 0;
}


static int get_arg(char ***pArgs, int next, char *buf, size_t sz)
{
	char **args = *pArgs;

	if ((next != 0) && (*args != NULL)) {
		args++;
	}

	if (*args == NULL) {
		fprintf(stderr, "route: argument required\n");
		return -1;
	}

	for (char *p = *args; ((*p != '\0') && (sz > 0)); --sz) {
		*(buf++) = *(p++);
	}

	if (sz == 0) {
		fprintf(stderr, "route: invalid argument\n");
		return -1;
	}

	*buf = '\0';
	*pArgs = args + 1;

	return 0;
}


static int psh_routeSet(int action, int options, char **args, const char *argLast)
{
	struct rtentry rt = { 0 };
	char buf[sizeof("255.255.255.255/32")];
	char dev[IFNAMSIZ];
	int err = 0;
	int isnet = 0;
	int setfl = 0;
	int skfd = -1;

	if (*args != NULL) {
		if (strcmp(*args, "-net") == 0) {
			isnet = 1;
			args++;
		}
		else if (strcmp(*args, "-host") == 0) {
			isnet = 0;
			args++;
		}
	}

	if (get_arg(&args, 0, buf, sizeof(buf)) < 0) {
		psh_routeUsage();
		return -1;
	}

	if (strcmp(buf, "default") != 0) {
		char *cidr = strchr(buf, '/');

		/* Parse optional CIDR notation */
		if (cidr != NULL) {
			struct in_addr *mask = &((struct sockaddr_in *)&rt.rt_genmask)->sin_addr;
			unsigned char prefixLen = 0;
			*(cidr++) = '\0';
			for (int i = 0; (i < 2) && (isdigit(*cidr) != 0); i++) {
				prefixLen = prefixLen * 10 + (*cidr - '0');
				cidr++;
			}

			if ((cidr[0] != '\0') || (prefixLen > 32)) {
				fprintf(stderr, "route: invalid prefix length\n");
				return -1;
			}

			mask->s_addr = 0;
			for (int i = 0; i < prefixLen; ++i) {
				mask->s_addr |= htonl(1u << (31 - i));
			}

			if ((isnet != 0) && (prefixLen == 32)) {
				psh_routeUsage();
				return -1;
			}

			setfl |= SETFL_MASK;
		}

		if (inet_pton(AF_INET, buf, &((struct sockaddr_in *)&rt.rt_dst)->sin_addr) != 1) {
			fprintf(stderr, "route: '%s' invalid target\n", buf);
			psh_routeUsage();
			return -1;
		}
		setfl |= SETFL_ADDR;
	}
	else {
		/* default route: 0.0.0.0/0 */
		((struct sockaddr_in *)&rt.rt_dst)->sin_addr.s_addr = 0;
		((struct sockaddr_in *)&rt.rt_gateway)->sin_addr.s_addr = 0;
		setfl = SETFL_ADDR | SETFL_MASK;
	}

	while (*args != NULL) {
		/*
		 * Order of (metric, netmask, gw, dev) route modifiers doesn't matter, but may occur once.
		 */
		if (((setfl & SETFL_MASK) == 0) && ((strcmp(*args, "netmask") == 0) || (strcmp(*args, "genmask") == 0) || (strcmp(*args, "mask") == 0))) {
			if (get_arg(&args, 1, buf, sizeof(buf) - 3) < 0) {
				err = 1;
				break;
			}
			if (inet_pton(AF_INET, buf, &((struct sockaddr_in *)&rt.rt_genmask)->sin_addr) != 1) {
				fprintf(stderr, "route: '%s' invalid mask\n", buf);
				err = 1;
				break;
			}
			setfl |= SETFL_MASK;
			continue;
		}
		if (((setfl & SETFL_GATEWAY) == 0) && ((strcmp(*args, "gateway") == 0) || (strcmp(*args, "gw") == 0) || (strcmp(*args, "via") == 0))) {
			if (get_arg(&args, 1, buf, sizeof(buf) - 3) < 0) {
				err = 1;
				break;
			}
			if (inet_pton(AF_INET, buf, &((struct sockaddr_in *)&rt.rt_gateway)->sin_addr) != 1) {
				fprintf(stderr, "route: '%s' invalid gateway\n", buf);
				err = 1;
				break;
			}
			rt.rt_flags |= RTF_GATEWAY;
			setfl |= SETFL_GATEWAY;
			continue;
		}
		if (((setfl & SETFL_METRIC) == 0) && (strcmp(*args, "metric") == 0)) {
			if (get_arg(&args, 1, buf, sizeof(buf)) < 0) {
				err = 1;
				break;
			}
			setfl |= SETFL_METRIC;
			continue;
		}
		if (((setfl & SETFL_REJECT) == 0) && (strcmp(*args, "reject") == 0)) {
			args++;
			setfl |= SETFL_REJECT;
			rt.rt_flags |= RTF_REJECT;
			continue;
		}
		if (((setfl & SETFL_DEVICE) == 0) && ((strcmp(*args, "device") == 0) || (strcmp(*args, "dev") == 0))) {
			if (get_arg(&args, 1, dev, sizeof(dev)) < 0) {
				err = 1;
				break;
			}
			setfl |= SETFL_DEVICE;
			continue;
		}

		/*
		 * If 'dev name' If is the last option on the command line, the word 'dev' may be omitted
		 */
		if (((setfl & SETFL_DEVICE) == 0) && (*args == argLast)) {
			if (get_arg(&args, 0, dev, sizeof(dev)) < 0) {
				err = 1;
				break;
			}
			setfl |= SETFL_DEVICE;
			continue;
		}

		fprintf(stderr, "route: '%s' invalid specifier\n", *args);
		psh_routeUsage();
		return -1;
	}

	if (err != 0) {
		psh_routeUsage();
		return -1;
	}

	rt.rt_flags |= RTF_UP;
	rt.rt_dev = dev;
	rt.rt_metric = 100;

	skfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (skfd < 0) {
		perror("socket");
		return -1;
	}

	if (action == act_add) {
		if (ioctl(skfd, SIOCADDRT, &rt) < 0) {
			perror("SIOCADDRT");
			close(skfd);
			return -1;
		}
	}
	else if (action == act_del) {
		if (ioctl(skfd, SIOCDELRT, &rt) < 0) {
			perror("SIOCDELRT");
			close(skfd);
			return -1;
		}
	}
	else {
		fprintf(stderr, "invalid action\n");
	}
	close(skfd);

	return 0;
}


static int psh_routeMain(int argc, char **argv)
{
	int action = act_none;
	int options = 0;
	int res = -1;
	const char *argLast = (argc > 0) ? argv[argc - 1] : argv[0];

	for (;;) {
		int opt = getopt(argc, argv, "hn");
		if (opt == -1) {
			break;
		}

		switch (opt) {
			case 'n':
				options |= OPT_NUMERIC;
				break;

			case 'h':
			default:
				psh_routeUsage();
				return EXIT_FAILURE;
		}
	}

	argv += optind;
	argc -= optind;

	if (*argv == NULL) {
		action = act_show;
		res = psh_routeShow(options);
	}
	else {
		if (strcmp(*argv, "add") == 0) {
			action = act_add;
		}
		else if (strcmp(*argv, "del") == 0) {
			action = act_del;
		}
		else {
			psh_routeUsage();
		}
	}

	if (action > act_show) {
		res = psh_routeSet(action, options, ++argv, argLast);
	}

	return (res < 0) ? EXIT_FAILURE : EXIT_SUCCESS;
}


void __attribute__((constructor)) route_registerapp(void)
{
	static psh_appentry_t app = { .name = "route", .run = psh_routeMain, .info = psh_routeInfo };
	psh_registerapp(&app);
}
