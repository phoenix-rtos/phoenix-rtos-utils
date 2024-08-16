/* FIXME: quick hack before better TLS implementation in phoenix is added */

#ifndef	RTLD_PHOENIX_LWP_H
#define	RTLD_PHOENIX_LWP_H


#include "include/NetBSD/tls.h"
#include <string.h>
#include <sys/threads.h>


#define __HAVE___LWP_GETPRIVATE_FAST

#define STR_HELPER(a) #a
#define STR(a)        STR_HELPER(a)
#define DBG()         debug(__FILE__ ":" STR(__LINE__) "\n")
// #define DBG()


extern size_t _rtld_tls_static_space;


#define _lwp_self gettid


static inline void *__lwp_getprivate_fast(void)
{
	void *tcb;

	__asm__ volatile("movl %%gs:0, %0" : "=r"(tcb));

	return tcb;
}

static inline void _lwp_setprivate(void *prv)
{
	void *privatePtr = __lwp_getprivate_fast();
    // Copy static space and tcb.
	memcpy(((char *)privatePtr) - _rtld_tls_static_space,
           ((char *)prv)        - _rtld_tls_static_space,
           _rtld_tls_static_space + sizeof(struct tls_tcb));
	// Update self pointer.
	struct tls_tcb *tcb = (struct tls_tcb *)privatePtr;
	tcb->tcb_self = tcb;
}

#endif
