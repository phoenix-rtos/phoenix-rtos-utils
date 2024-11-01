/* FIXME: quick hack before better TLS implementation in phoenix is added */

#ifndef	RTLD_PHOENIX_LWP_H
#define	RTLD_PHOENIX_LWP_H


#include "include/NetBSD/tls.h"
#include <string.h>
#include <sys/threads.h>


#define __HAVE___LWP_GETPRIVATE_FAST

extern size_t _rtld_tls_static_space;


#define _lwp_self gettid

#ifdef __HAVE___LWP_GETTCB_FAST

#if defined(__riscv)

static inline void *__lwp_gettcb_fast(void)
{
	void *tcb;

	__asm__ volatile("addi %[tcb], tp, %[offset]"
	    :	[tcb] "=r" (tcb)
	    :	[offset] "n" (-sizeof(struct tls_tcb)));

	return tcb;
}

#endif

#else /*__HAVE___LWP_GETTCB_FAST */

#if defined(__i386__)

static inline void *__lwp_getprivate_fast(void)
{
	void *tcb;

	__asm__ volatile("movl %%gs:0, %0" : "=r"(tcb));

	return tcb;
}

#elif defined(__arm__)

static inline void *__lwp_getprivate_fast(void)
{
	void *tcb;

	__asm__ volatile("mrc p15, 0, %0, cr13, cr0, 3" : "=r"(tcb));

	return tcb;
}

#elif defined(__sparc__)

static inline void *__lwp_getprivate_fast(void)
{
	register void *tcb asm("g7");

	return tcb;
}

#endif

#endif /*__HAVE___LWP_GETTCB_FAST */


#ifdef __HAVE___LWP_SETTCB

static inline void __lwp_settcb(void *tcb)
{
	void *tcbPtr = __lwp_gettcb_fast();
#ifdef __HAVE_TLS_VARIANT_I
	memcpy(tcbPtr, tcb, _rtld_tls_static_space + sizeof(struct tls_tcb));
#else
	/* Copy static space and tcb. */
	memcpy(((char *)tcbPtr) - _rtld_tls_static_space,
           ((char *)tcb)    - _rtld_tls_static_space,
           _rtld_tls_static_space + sizeof(struct tls_tcb));
	/* Update self pointer. */
	struct tls_tcb *tcb = (struct tls_tcb *)tcbPtr;
	tcb->tcb_self = tcb;
#endif
}

#else /* __HAVE___LWP_SETTCB */

static inline void _lwp_setprivate(void *prv)
{
	void *privatePtr = __lwp_getprivate_fast();
#ifdef __HAVE_TLS_VARIANT_I
	memcpy(privatePtr, prv, _rtld_tls_static_space + sizeof(struct tls_tcb));
#else
    /* Copy static space and tcb. */
	memcpy(((char *)privatePtr) - _rtld_tls_static_space,
           ((char *)prv)        - _rtld_tls_static_space,
           _rtld_tls_static_space + sizeof(struct tls_tcb));
	/* Update self pointer. */
	struct tls_tcb *tcb = (struct tls_tcb *)privatePtr;
	tcb->tcb_self = tcb;
#endif
}

#endif /* __HAVE___LWP_SETTCB */

#endif
