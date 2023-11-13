# $NetBSD: Makefile.inc,v 1.2 2018/12/27 18:58:14 christos Exp $


SRCS+= $(addprefix $(call my-dir), rtld_start.S mdreloc.c)

# XXX Should not be in CPPFLAGS!
CFLAGS+=	-fpic

CFLAGS+=	-DELFSIZE=64
CFLAGS+=	-DRTLD_COMMON_CALL_IFUNC_REL

LDFLAGS+=	-Wl,-e,.rtld_start
