/*
 * Phoenix-RTOS
 *
 * Phoenix-RTOS SHell
 *
 * Copyright 2020 Phoenix Systems
 * Author: Maciej Purski, Lukasz Kosinski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _PSH_H_
#define _PSH_H_

/* Exit code based on POSIX - Shell Command Language */
#define PSH_UNKNOWN_CMD 127


typedef struct psh_app {
	const char name[11];
	int (*const run)(int argc, char **argv);
	void (*const info)(void);
	struct psh_app *next;
} psh_appentry_t;


typedef struct {
	psh_appentry_t *pshapplist;
	char *ttydev;
	volatile unsigned char sigint;  /* Received SIGINT */
	volatile unsigned char sigquit; /* Received SIGQUIT */
	volatile unsigned char sigstop; /* Received SIGTSTP */
	pid_t tcpid;
	int exitStatus;
} psh_common_t;


extern psh_common_t psh_common;


/* Converts n = x * base ^ y to a short binary(base 2) or SI(base 10) prefix notation */
/* (rounds n to prec decimal places and cuts trailing zeros), e.g. */
/* utils_prefix(10, -15496, 3, 2, buff) saves "-15.5M" in buff */
/* utils_prefix(2, 2000, 10, 3, buff) saves "1.953M" in buff */
extern int psh_prefix(unsigned int base, int x, int y, unsigned int prec, char *buff);


extern void _psh_exit(int code);


extern void psh_registerapp(psh_appentry_t *newapp);


extern const psh_appentry_t *psh_findapp(char *appname);


extern const psh_appentry_t *psh_applist_first(void);


extern const psh_appentry_t *psh_applist_next(const psh_appentry_t *current);


extern size_t psh_write(int fd, const void *buf, size_t count);


extern int psh_ttyopen(const char *dev);


#endif
