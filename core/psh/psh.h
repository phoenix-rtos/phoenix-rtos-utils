/*
 * Phoenix-RTOS
 *
 * Phoenix Shell top
 *
 * Copyright 2020 Phoenix Systems
 * Author: Maciej Purski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _PSH_H_
#define _PSH_H_

/* Prefix base */
enum { BP = 2, SI = 10 };

int psh_convert(unsigned int base, int x, int y, unsigned int prec, char *buff);
char *psh_nextString(char *buff, unsigned int *size);
int psh_ls(char *args);

#endif
