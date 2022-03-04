/*
 * Phoenix-RTOS
 *
 * date - print or set the system date and time
 *
 * Copyright 2022 Phoenix Systems
 * Author: Mateusz Niewiadomski
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

#include "../psh.h"


enum mode { mode_skip,
	mode_help,
	mode_print,
	mode_set,
	mode_parse };


static void psh_date_info(void)
{
	printf("print/set the system date and time");
}


static void psh_date_help(const char *prog)
{
	printf(
		"Usage: %s [-h] [-s EPOCH] [-d @EPOCH] [+FORMAT]\n"
		"  -h:  shows this help message\n"
		"  -s:  set system time described by EPOCH (POSIX time format)\n"
		"  -d:  display time described by EPOCH (POSIX time format)\n"
		"  FORMAT: string with POSIX date formatting characters\n"
		"NOTE: FORMAT string not supported by options: '-s', '-d'\n",
		prog);
}


static int psh_date_print(const struct timeval *tv, const char *format)
{
	struct tm *tinfo;
	const char *defformat = "+%a, %d %b %y %H:%M:%S";
	char buf[64];

	if (format == NULL) {
		format = defformat;
	}
	else if (format[0] != '+') {
		fprintf(stderr, "date: invalid format '%s'\n", format);
		return 1;
	}

	tinfo = localtime(&(tv->tv_sec));
	if (tinfo == NULL) {
		fprintf(stderr, "date: time get error!\n");
		return 1;
	}

	/* passing `&format[1]` for omission of '+' at the beginning */
	if (strftime(buf, sizeof(buf), &format[1], tinfo) == 0) {
		fprintf(stderr, "date: '%s' expansion too long\n", format);
		return 1;
	}
	printf("%s\n", buf);

	return 0;
}


static int psh_date_get(const char *format)
{
	struct timeval tv;

	time(&(tv.tv_sec));
	tv.tv_usec = 0;
	return psh_date_print(&tv, format);
}


static int psh_date_convert(const char *timestring, const char *format, struct timeval *tv)
{
	char *end;

	if (timestring == NULL) {
		return 1;
	}

	/* FIXME: strptime() should be used below: unimplemented in libphoenix */
	if (format != NULL) {
		fprintf(stderr, "date: chosen option does not support FORMAT\n");
		return 1;
	}
	if (timestring[0] == '@') {
		tv->tv_usec = 0;
		tv->tv_sec = strtoull(&timestring[1], &end, 10);
		if ((end != timestring) && (*end == '\0')) {
			return 0;
		}
	}

	fprintf(stderr, "date: invalid date '%s'\n", timestring);
	return 1;
}


static int psh_date_parse(const char *timestring, const char *format)
{
	struct timeval tv;

	if (timestring != NULL && timestring[0] != '@') {
		fprintf(stderr, "date: invalid date '%s'\n", timestring);
		return 1;
	}

	/* error message printed by psh_date_convert() */
	if (psh_date_convert(timestring, format, &tv) != 0) {
		return 1;
	}

	return psh_date_print(&tv, NULL);
}


static int psh_date_set(const char *timestring, const char *format)
{
	struct timeval tv;
	struct timezone tz = { .tz_dsttime = 0, .tz_minuteswest = 0 };

	/* error message printed by psh_date_convert() */
	if (psh_date_convert(timestring, format, &tv) != 0) {
		return 1;
	}

	if (settimeofday(&tv, &tz) != 0) {
		fprintf(stderr, "date: time set failed: %s\n", strerror(errno));
		return 1;
	}

	return psh_date_get(NULL);
}


static int psh_date(int argc, char **argv)
{
	int c, status = 1;
	const char *timestring = NULL, *format = NULL;
	enum mode m = mode_print;

	while ((c = getopt(argc, argv, "hs:d:")) != -1) {
		switch (c) {
			case 'h':
				m = mode_help;
				break;

			case 's':
				m = mode_set;
				timestring = optarg;
				break;

			case 'd':
				m = mode_parse;
				timestring = optarg;
				break;

			default:
				m = mode_skip;
				break;
		}
	}

	/* evaluate additional arguments/format */
	if (m != mode_skip) {
		if (argc > optind + 1) {
			m = mode_skip;
			fprintf(stderr, "Unrecognized argument: %s\n", argv[optind + 1]);
		}
		else if (argc != optind) {
			format = argv[optind];
		}
	}

	/* run proper option */
	switch (m) {
		case mode_set:
			status = psh_date_set(timestring, format);
			break;

		case mode_print:
			status = psh_date_get(format);
			break;

		case mode_parse:
			status = psh_date_parse(timestring, format);
			break;

		case mode_help:
			psh_date_help(argv[0]);
			status = 0;
			break;

		default:
			/* everything set for default */
			break;
	}

	return (status == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}


void __attribute__((constructor)) date_registerapp(void)
{
	static psh_appentry_t app = { .name = "date", .run = psh_date, .info = psh_date_info };
	psh_registerapp(&app);
}
