/*
 * Phoenix-RTOS
 *
 * dd - converts and copies a file according to operands
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
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/time.h>

#include "../psh.h"


/* clang-format off */

struct param_s {
	char *name;
	int value;
};


enum { param_none = 0, param_if, param_of, param_bs,
	param_conv, param_count, param_seek, param_skip };


static const struct param_s params[] = {
	{ "if", param_if },
	{ "of", param_of },
	{ "bs", param_bs },
	{ "conv", param_conv },
	{ "count", param_count },
	{ "seek", param_seek },
	{ "skip", param_skip },
	{ NULL, param_none }
};


enum { conv_none = 0, conv_nocreat, conv_notrunc };


static const struct param_s convs[] = {
	{ "nocreat", conv_nocreat },
	{ "notrunc", conv_notrunc },
	{ NULL, conv_none }
};

/* clang-format on */


static void psh_dd_info(void)
{
	printf("copy a file according to the operands");
}


static void psh_dd_usage(void)
{
	printf(
		"Usage: dd [OPERAND]...\n"
		"\tif=FILE     read from FILE instead of stdin\n"
		"\tof=FILE     write to FILE instead of stdout\n"
		"\tbs=BYTE     read/write block size of BYTES bytes\n"
		"\tcount=N     copy only N input blocks\n"
		"\tseek=N      skip N bs-sized blocks at start of output\n"
		"\tskip=N      skip N bs-sized blocks at start of input\n"
		"\tconv=CONVS  comma-separated list of supported conversions:\n"
		"\t            e.g. nocreat,notrunc\n");
}


static ssize_t getnumber(const char *numstr)
{
	char *str;
	ssize_t retval;
	ssize_t mult = 1;
	ssize_t value = strtoul(numstr, &str, 10);

	if (numstr == str) {
		return -EINVAL;
	}

	switch (*(str++)) {
		case 'M':
			mult = 1024 * 1024;
			break;

		case 'k':
			mult = 1024;
			break;

		case 'b':
			mult = 512;
			break;

		case 'w':
			mult = 2;
			break;

		case 'c':
			/* mult *= 1; */
			break;

		case '\0':
			return value;

		default:
			return -1;
	}

	if (*str != '\0') {
		return -EINVAL;
	}

	retval = value * mult;
	if (value > SSIZE_MAX / mult) {
		return -EOVERFLOW;
	}

	return retval;
}


static int getconvmode(mode_t *m, const char *str)
{
	const char *end;
	const struct param_s *par;
	mode_t mode = *m;

	for (end = str; *end != '\0'; str = end + 1) {
		end = strchr(str, ',');
		if (end == NULL) {
			end = str + strlen(str);
		}

		for (par = convs; par->name != NULL; par++) {
			size_t len = end - str;
			if ((strncmp(str, par->name, len) == 0) && (par->name[len] == '\0')) {
				break;
			}
		}

		switch (par->value) {
			case conv_nocreat:
				mode &= ~O_CREAT;
				break;

			case conv_notrunc:
				mode &= ~O_TRUNC;
				break;

			default:
				return -EINVAL;
		}
	}

	*m = mode;

	return EOK;
}


static int psh_dd(int argc, char **argv)
{
	struct timespec start_time, end_time;
	const struct param_s *par;
	const char *str, *cp;
	char *buf, *p;
	int infd, outfd;
	ssize_t incc, outcc;
	ssize_t intotal, outtotal;
	double elapsed_time;

	const char *infile = NULL;
	const char *outfile = NULL;
	mode_t outmode = O_CREAT | O_TRUNC;
	ssize_t tmp, count = SSIZE_MAX;
	ssize_t seekval = 0;
	ssize_t skipval = 0;
	ssize_t inmax = 0;
	int err = 0, blocksz = 512;

	if ((argc > 1) && (argv[1][0] == '-') && (argv[1][1] == 'h')) {
		psh_dd_usage();
		return EXIT_SUCCESS;
	}

	while (--argc > 0) {
		str = *(++argv);
		cp = strchr(str, '=');
		if (cp == NULL) {
			fprintf(stderr, "Bad dd argument\n");
			return EXIT_FAILURE;
		}

		for (par = params; par->name != NULL; par++) {
			size_t len = cp - str;
			if ((strncmp(str, par->name, len) == 0) && (par->name[len] == '\0')) {
				break;
			}
		}

		cp++;

		switch (par->value) {
			case param_if:
				if (infile != NULL) {
					fprintf(stderr, "Multiple input files illegal\n");
					return EXIT_FAILURE;
				}
				infile = cp;
				break;

			case param_of:
				if (outfile != NULL) {
					fprintf(stderr, "Multiple output files illegal\n");
					return EXIT_FAILURE;
				}
				outfile = cp;
				break;

			case param_bs:
				blocksz = getnumber(cp);
				if (blocksz <= 0) {
					fprintf(stderr, "Bad block size value\n");
					return EXIT_FAILURE;
				}
				break;

			case param_conv:
				if (getconvmode(&outmode, cp) < 0) {
					fprintf(stderr, "Invalid conv symbol list\n");
					return EXIT_FAILURE;
				}
				break;

			case param_count:
				count = getnumber(cp);
				if (count < 0) {
					fprintf(stderr, "Bad count value\n");
					return EXIT_FAILURE;
				}
				break;

			case param_seek:
				seekval = getnumber(cp);
				if (seekval < 0) {
					fprintf(stderr, "Bad seek value\n");
					return EXIT_FAILURE;
				}
				break;

			case param_skip:
				skipval = getnumber(cp);
				if (skipval < 0) {
					fprintf(stderr, "Bad skip value\n");
					return EXIT_FAILURE;
				}
				break;

			default:
				fprintf(stderr, "Unknown dd parameter\n");
				return EXIT_FAILURE;
		}
	}

	if (skipval > 0) {
		tmp = skipval;
		skipval *= blocksz;
		if (tmp > SSIZE_MAX / blocksz) {
			fprintf(stderr, "Skip value overflowed\n");
			return EXIT_FAILURE;
		}
	}

	if (seekval > 0) {
		tmp = seekval;
		seekval *= blocksz;
		if (tmp > SSIZE_MAX / blocksz) {
			fprintf(stderr, "Seek value overflowed\n");
			return EXIT_FAILURE;
		}
	}

	if (count != SSIZE_MAX) {
		inmax = count * blocksz;
		if (count > SSIZE_MAX / blocksz) {
			fprintf(stderr, "Transfer total size overflowed\n");
			return EXIT_FAILURE;
		}
	}

	buf = malloc(blocksz);
	if (buf == NULL) {
		fprintf(stderr, "Cannot allocate buffer\n");
		return EXIT_FAILURE;
	}

	intotal = 0;
	outtotal = 0;

	infd = ((infile == NULL) ? fileno(stdin) : open(infile, O_RDONLY));
	infile = (infile == NULL) ? "stdin" : infile;
	if (infd < 0) {
		fprintf(stderr, "'%s': %s\n", infile, strerror(errno));
		free(buf);
		return EXIT_FAILURE;
	}

	outfd = ((outfile == NULL) ? fileno(stdout) : open(outfile, outmode | O_WRONLY, 0666));
	outfile = (outfile == NULL) ? "stdout" : outfile;
	if (outfd < 0) {
		fprintf(stderr, "'%s': %s\n", outfile, strerror(errno));
		if (infd != fileno(stdin)) {
			close(infd);
		}
		free(buf);
		return EXIT_FAILURE;
	}

	clock_gettime(CLOCK_MONOTONIC, &start_time);

	do {
		if (skipval > 0) {
			if (lseek(infd, skipval, 0) < 0) {
				while (skipval-- > 0) {
					incc = read(infd, buf, blocksz);
					if (incc < 0) {
						perror(infile);
						err = 1;
						break; /* goto cleanup */
					}
					if (incc == 0) {
						fprintf(stderr, "End of file while skipping\n");
						err = 1;
						break; /* goto cleanup */
					}
				}

				if (err == 1) {
					break;
				}
			}
		}

		if (seekval > 0) {
			if (lseek(outfd, seekval, 0) < 0) {
				perror(outfile);
				break;
			}
		}

		for (;;) {
			incc = read(infd, buf, blocksz);
			if (incc <= 0) {
				if ((incc < 0) && (errno == EINTR)) {
					continue;
				}

				break;
			}

			intotal += incc;
			p = buf;

			if (psh_common.sigint != 0) {
				fprintf(stderr, "Interrupted\n");
				err = 1;
				break; /* goto cleanup */
			}

			while (incc > 0) {
				outcc = write(outfd, p, incc);
				if (outcc < 0) {
					if (errno == EINTR) {
						continue;
					}
					perror(outfile);
					err = 1;
					break; /* goto cleanup */
				}
				outtotal += outcc;
				incc -= outcc;
				p += outcc;
			}

			if (err == 1) {
				break;
			}

			if ((inmax != 0) && (intotal >= inmax)) {
				break;
			}
		}

		if (err == 1) {
			break;
		}

		if (incc < 0) {
			perror(infile);
		}

	} while (0);

	clock_gettime(CLOCK_MONOTONIC, &end_time);

	/* cleanup: */
	if (infd != fileno(stdin)) {
		close(infd);
	}

	if (outfd != fileno(stdout)) {
		if (close(outfd) < 0) {
			perror(outfile);
		}
	}

	free(buf);

	fprintf(stderr, "%zu+%d records in\n",
		(size_t)(intotal / blocksz),
		(int)(intotal % blocksz) != 0);

	fprintf(stderr, "%zu+%d records out\n",
		(size_t)(outtotal / blocksz),
		(int)(outtotal % blocksz) != 0);

	elapsed_time = (end_time.tv_sec - start_time.tv_sec) + (end_time.tv_nsec - start_time.tv_nsec) / 1000000000.0;
	fprintf(stderr, "%zu byte%s copied, ", (size_t)outtotal, ((outtotal != 1) ? "s" : ""));
	if (elapsed_time > 0.0) {
		fprintf(stderr, "%.3f s, %.1f kB/s\n", elapsed_time, (double)outtotal / elapsed_time / 1024.0);
	}
	else {
		fprintf(stderr, "speed not estimated\n");
	}

	return EXIT_SUCCESS;
}


void __attribute__((constructor)) dd_registerapp(void)
{
	static psh_appentry_t app = { .name = "dd", .run = psh_dd, .info = psh_dd_info };
	psh_registerapp(&app);
}
