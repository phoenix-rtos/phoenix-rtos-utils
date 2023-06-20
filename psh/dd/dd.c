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
	ssize_t value;

	if (isdigit(*numstr) == 0) {
		return -EINVAL;
	}

	value = 0;
	while (isdigit(*numstr) != 0) {
		value = value * 10 + *(numstr++) - '0';
	}

	switch (*(numstr++)) {
		case 'M':
			value *= 1024 * 1024;
			break;

		case 'k':
			value *= 1024;
			break;

		case 'b':
			value *= 512;
			break;

		case 'w':
			value *= 2;
			break;

		case 'c':
			/* value *= 1; */
			break;

		case '\0':
			return value;

		default:
			return -1;
	}

	if (*numstr != '\0') {
		return -EINVAL;
	}

	return value;
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
	const struct param_s *par;
	const char *str, *cp;
	char *buf, *p;
	int infd, outfd, incc, outcc;
	ssize_t intotal, outtotal;

	const char *infile = NULL;
	const char *outfile = NULL;
	mode_t outmode = O_CREAT | O_TRUNC;
	ssize_t count = SSIZE_MAX;
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

	buf = malloc(blocksz);
	if (buf == NULL) {
		fprintf(stderr, "Cannot allocate buffer\n");
		return EXIT_FAILURE;
	}

	intotal = 0;
	outtotal = 0;

	infd = ((infile == NULL) ? fileno(stdin) : open(infile, O_RDONLY));
	if (infd < 0) {
		fprintf(stderr, "'%s': %s\n", infile, strerror(errno));
		free(buf);
		return EXIT_FAILURE;
	}

	outfd = ((outfile == NULL) ? fileno(stdout) : open(outfile, outmode | O_WRONLY, 0666));
	if (outfd < 0) {
		fprintf(stderr, "'%s': %s\n", outfile, strerror(errno));
		if (infd != fileno(stdin)) {
			close(infd);
		}
		free(buf);
		return EXIT_FAILURE;
	}

	do {
		if (skipval > 0) {
			if (lseek(infd, skipval * blocksz, 0) < 0) {
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
			if (lseek(outfd, seekval * blocksz, 0) < 0) {
				perror(outfile);
				break;
			}
		}

		if (count != SSIZE_MAX) {
			inmax = count * blocksz;
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

	printf("%zu+%d records in\n",
		(size_t)(intotal / blocksz),
		(int)(intotal % blocksz) != 0);

	printf("%zu+%d records out\n",
		(size_t)(outtotal / blocksz),
		(int)(outtotal % blocksz) != 0);

	return EXIT_FAILURE;
}


void __attribute__((constructor)) dd_registerapp(void)
{
	static psh_appentry_t app = { .name = "dd", .run = psh_dd, .info = psh_dd_info };
	psh_registerapp(&app);
}
