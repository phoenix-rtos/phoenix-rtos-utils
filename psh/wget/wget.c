/*
 * Phoenix-RTOS
 *
 * wget - downloads a file using HTTP
 *
 * Copyright 2022 Phoenix Systems
 * Author: Maciej Purski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/minmax.h>
#include <sys/time.h>

#include "../psh.h"


static struct {
	char buf[4096];
	char *readptr;
	int len;
	FILE *outfile;
	const char *path;
	char *host;
	const char *filename;
	int debug;
	int socket;
} common;


void psh_wgetinfo(void)
{
	printf("downloads a file using http");
}


static void wget_help(void)
{
	printf("Usage: wget [options] ... URL\n"
		   "Options\n"
		   "  -h:  prints help\n"
		   "  -O:  output file\n");
}


static int wget_parseUrl(const char *url)
{
	const char httpProto[] = "http://";
	const char *tmp, *bare;

	if (strstr(url, "://") == NULL) {
		/* No protocol specified, assume HTTP */
		bare = url;
	}
	else if (strncmp(url, httpProto, strlen(httpProto)) == 0) {
		bare = url + strlen(httpProto);
	}
	else {
		printf("wget: Unsupported protocol!\n");
		return -1;
	}

	tmp = strrchr(bare, '/');
	if (tmp == NULL || tmp[1] == '\0') {
		fprintf(stderr, "wget: url missing filename!\n");
		return -1;
	}

	common.filename = tmp + 1;

	/* TODO: Decode URL */
	tmp = strchr(bare, '/');
	common.path = tmp + 1;
	common.host = strndup(bare, tmp - bare);
	if (common.host == NULL) {
		fprintf(stderr, "wget: Out of memory!\n");
		return -1;
	}

	return 0;
}


static uint32_t *wget_aiToAddr(struct addrinfo *ai)
{
	return &(((struct sockaddr_in *)ai->ai_addr)->sin_addr.s_addr);
}


static int wget_connect(const char *host)
{
	struct addrinfo *res;
	struct addrinfo hints = { 0 };
	char hostaddr[INET_ADDRSTRLEN];
	int fd = -1;

	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	printf("Resolving %s... ", host);
	fflush(stdout);
	if (getaddrinfo(host, "80", &hints, &res) != 0) {
		printf("Failed\n");
		return -1;
	}

	if (inet_ntop(res->ai_family, wget_aiToAddr(res), hostaddr, sizeof(hostaddr)) == NULL) {
		fprintf(stderr, "nslookup: Error while converting address!\n");
		freeaddrinfo(res);
		return -1;
	}
	printf("%s\n", hostaddr);

	printf("Connecting to %s|%s|:80... ", host, hostaddr);
	fflush(stdout);

	fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (fd < 0) {
		printf("Failed\n");
		freeaddrinfo(res);
		return -1;
	}

	if (connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
		close(fd);
		freeaddrinfo(res);
		return -1;
	}
	printf("Connected\n");

	freeaddrinfo(res);

	return fd;
}


static int wget_request(void)
{
	char buf[256];
	int bytes = 0, i = 0, ret;

	bytes = snprintf(buf, sizeof(buf), "GET /%s HTTP/1.1\r\nHost: %s\r\nUser-Agent: Wget\r\n\r\n",
		common.path, common.host);
	if ((bytes < 0) || (bytes >= sizeof(buf))) {
		fprintf(stderr, "wget: Request url too large!\n");
		return -1;
	}

	while (i < bytes) {
		ret = write(common.socket, buf + i, bytes - i);
		if (ret <= 0) {
			return -1;
		}

		i += ret;
	}

	if (common.debug) {
		printf("--------Request--------\n%*s\n", bytes, buf);
	}

	return 0;
}


static size_t wget_file(size_t len)
{
	ssize_t ret;
	size_t left = len;
	size_t bytes;
	int i = 0;

	/* Save what is left in the buffer */
	if (common.readptr < common.buf + common.len) {
		bytes = common.buf + common.len - common.readptr;
		if (fwrite(common.readptr, 1, bytes, common.outfile) != bytes) {
			fprintf(stderr, "wget: Write failed\n");
			return -1;
		}
		left -= bytes;
	}

	while (left > 0) {
		ret = read(common.socket, common.buf, min(sizeof(common.buf), left));
		if (ret <= 0) {
			fprintf(stderr, "\nwget: read from socket failed!\n");
			return -1;
		}

		left -= ret;

		if (fwrite(common.buf, 1, ret, common.outfile) != ret) {
			fprintf(stderr, "\nwget: Write failed\n");
			return -1;
		}
		if (i == 0) {
			printf("\rWritten: %8zu/%zu", len - left, len);
			fflush(stdout);
		}
		i = (i + 1) % 8;
	}
	printf("\rWritten: %8zu/%zu\n", len - left, len);

	return 0;
}


static int wget_buf(void)
{
	int ret;

	/* Move not consumed part of the buffer */
	common.len = common.len - (common.readptr - common.buf);
	memmove(common.buf, common.readptr, common.len);
	common.readptr = common.buf;

	ret = read(common.socket, common.buf + common.len, sizeof(common.buf) - common.len);
	if (ret <= 0) {
		return -1;
	}

	common.len += ret;

	return 0;
}


static char *wget_hdrnext(void)
{
	char *hdr = NULL, *tmp;

	while (hdr == NULL) {
		tmp = memchr(common.readptr, '\n', common.buf + common.len - common.readptr);
		if (tmp == NULL) {
			/* Carry on reading */
			if (wget_buf() != 0) {
				return NULL;
			}
			continue;
		}

		*(tmp - 1) = '\0';
		hdr = common.readptr;
		common.readptr = tmp + 1;
	}

	return hdr;
}


static size_t wget_parsehdrs(void)
{
	const char contenthdr[] = "content-length:";
	char *hdr = NULL, *endptr, *num;
	size_t contentLen = 0;

	/* Read headers until no more data is available or empty line is found */
	do {
		hdr = wget_hdrnext();
		if (hdr == NULL) {
			return -1;
		}

		if (strncasecmp(hdr, contenthdr, strlen(contenthdr)) == 0) {
			num = hdr + strlen(contenthdr);
			/*
			 * According to RFC2616: "The field value MAY be preceded by ANY amount of LWS."
			 * TODO: Handle multi-line headers
			 */
			while (*num != '\0' && (*num == ' ' || *num == '\t')) {
				num++;
			}

			if (*num == '\0') {
				break;
			}
			contentLen = strtoul(num, &endptr, 0);
			if (*endptr != '\0') {
				break;
			}
		}

		if (common.debug) {
			printf("%s\n", hdr);
		}
	} while (*hdr != '\0');

	return contentLen;
}


static char *wget_response(void)
{
	char *status = NULL, *tmp;
	int bytes;

	common.len = 0;
	common.readptr = common.buf;
	/* Read first line */
	while (status == NULL) {
		bytes = read(common.socket, common.buf + common.len, sizeof(common.buf) - common.len);
		if (bytes <= 0) {
			return NULL;
		}

		if (common.len == 0 && common.debug) {
			printf("\n-------Response-------\n");
		}

		if (common.debug) {
			printf("%*s", bytes, common.buf);
		}
		common.len += bytes;

		tmp = memchr(common.buf, '\n', common.len);
		/* We have not received a whole line yet, carry on reading */
		if (tmp == NULL) {
			common.readptr = common.buf + bytes;
			continue;
		}

		/* Omit \r\n */
		common.readptr = tmp + 1;

		*(tmp - 1) = '\0';
		tmp = strstr(common.buf, "HTTP");
		if (tmp == NULL) {
			return NULL;
		}

		tmp = strchr(tmp, ' ');
		if (tmp == NULL) {
			return NULL;
		}

		status = tmp + 1;
	}


	return status;
}


static void wget_cleanup(void)
{
	/* Cursor enable */
	printf("\033[?25h");

	fclose(common.outfile);
	close(common.socket);
	free(common.host);
}


int psh_wget(int argc, char **argv)
{
	const char *url;
	const char *output = NULL;
	char *status, *endptr;
	size_t len;
	int statnum;
	int c;
	struct timespec ts;
	time_t begin, end, delta;
	int ret;

	while ((c = getopt(argc, argv, "O:dh")) != -1) {
		switch (c) {
			case 'O':
				output = optarg;
				break;
			case 'd':
				common.debug = 1;
				break;
			default:
			case 'h':
				wget_help();
				return 0;
		}
	}

	if (argc - optind != 1) {
		fprintf(stderr, "URL missing!\n");
		return 2;
	}

	url = argv[optind];

	if (wget_parseUrl(url) != 0) {
		return 2;
	}

	/* By default save the file to the current directory with the name from url */
	if (output == NULL) {
		output = common.filename;
	}

	common.outfile = fopen(output, "w+");
	if (common.outfile == NULL) {
		fprintf(stderr, "Fail to open file %s\n", output);
		return 2;
	}

	/* Disable cursor */
	printf("\033[?25l");

	common.socket = wget_connect(common.host);
	if (common.socket < 0) {
		/* Cursor enable */
		printf("\033[?25h");
		fprintf(stderr, "wget: Fail to connect to host!\n");
		fclose(common.outfile);
		return 1;
	}

	if (wget_request() < 0) {
		fprintf(stderr, "wget: HTTP request sending failed\n");
		wget_cleanup();
		return 1;
	}

	printf("HTTP request sent, awaiting response... ");
	fflush(stdout);

	status = wget_response();
	if (status == NULL) {
		fprintf(stderr, "wget: Failed to get response!\n");
		wget_cleanup();
		return 1;
	}

	statnum = strtol(status, &endptr, 10);
	if (*endptr != ' ') {
		fprintf(stderr, "wget: Wrong status format!\n");
		wget_cleanup();
		return 1;
	}

	printf("%s\n", status);

	/* TODO: Handle other statuses properly */
	if (statnum < 200 || statnum >= 300) {
		wget_cleanup();
		return 1;
	}

	len = wget_parsehdrs();
	printf("Length: %zu\n", len);
	if (len == 0) {
		printf("Nothing to be copied\n");
		wget_cleanup();
		return 0;
	}

	printf("Saving to: '%s'\n", output);

	clock_gettime(CLOCK_MONOTONIC, &ts);
	begin = ts.tv_sec * 1000000 + ts.tv_nsec / 1000;

	if (wget_file(len) == 0) {
		clock_gettime(CLOCK_MONOTONIC, &ts);
		end = ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
		delta = end - begin;

		printf("Downloaded %zu bytes in %lld.%03llds\n", len, delta / 1000000, (delta - (delta / 1000000) * 1000000) / 1000);
		ret = 0;
	}
	else {
		ret = 1;
	}

	wget_cleanup();

	return ret;
}


void __attribute__((constructor)) wget_registerapp(void)
{
	static psh_appentry_t app = { .name = "wget", .run = psh_wget, .info = psh_wgetinfo };
	psh_registerapp(&app);
}
