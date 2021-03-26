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


typedef struct {
	volatile unsigned char sigint;  /* Received SIGINT */
	volatile unsigned char sigquit; /* Received SIGQUIT */
	volatile unsigned char sigstop; /* Received SIGTSTP */
	char* unkncmd;
	char* passwd;
} psh_common_t;


extern psh_common_t psh_common;


/* Converts n = x * base ^ y to a short binary(base 2) or SI(base 10) prefix notation */
/* (rounds n to prec decimal places and cuts trailing zeros), e.g. */
/* utils_prefix(10, -15496, 3, 2, buff) saves "-15.5M" in buff */
/* utils_prefix(2, 2000, 10, 3, buff) saves "1.953M" in buff */
extern int psh_prefix(unsigned int base, int x, int y, unsigned int prec, char *buff);


extern void _psh_exit(int code);


#endif
