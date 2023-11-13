#	$NetBSD: Makefile.inc,v 1.22 2018/12/27 18:58:14 christos Exp $

SRCS+= $(addprefix $(call my-dir), rtld_start.S mdreloc.c)

# XXX Should not be in CPPFLAGS!
CFLAGS+=	-fpic

CFLAGS+=	-DELFSIZE=32
CFLAGS+=	-DRTLD_COMMON_CALL_IFUNC_REL

# FIXME: Investigate
# .if !empty(LDELFSO_MACHINE_ARCH:Mearm*)
# CPPFLAGS+=	-DELF_NOTE_MARCH_DESC=\"${LDELFSO_MACHINE_ARCH}\"
# SRCS+=		find_exidx.c
# .endif

LDFLAGS+=	-Wl,-e,.rtld_start
