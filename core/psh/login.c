/*
 * Phoenix-RTOS
 *
 * login - allows/disallows access to Phoenix SHell 
 *
 * Copyright 2021 Phoenix Systems
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
#include <unistd.h>

#include "psh.h"


int psh_login(void){
	char c, *usrpasswd;
	unsigned int passsz = 0;

	/* allocate memory for user-given password */
	if ((usrpasswd = calloc(strlen(psh_common.passwd),sizeof(char)+1)) == NULL )
	{
		fprintf(stderr,"psh: fail to allocate memory for login");
		return -ENOMEM;
	}

	for (;;)
	{
		if( !passsz ){
			printf("Enter password: ");
			fflush(0);
		}

		read(STDIN_FILENO, &c, 1);

		if ( c == '\n' )
		{
			if (  (passsz == strlen(psh_common.passwd)) && !strcmp(usrpasswd,psh_common.passwd) )
			{
				break;	/* correct password is given, success exit */
			}
			else
			{
				memset(usrpasswd,'\0',strlen(usrpasswd));
				passsz=0;
				fprintf(stderr,"psh: wrong password!\n");
			}
		}
		else if ( passsz && ((c == '\b') || (c == '\177')) )
		{
			if( passsz < strlen(psh_common.passwd) )
				usrpasswd[passsz--] = '\0';
		}
		else
		{
			if( passsz < strlen(psh_common.passwd) )
				usrpasswd[passsz] = c;
			passsz++;
		}
	}

	free(usrpasswd);
	printf("Correct password!\n");
	return 0;
}

