#	$NetBSD: Makefile.inc,v 1.15 2018/04/03 21:10:27 joerg Exp $

SRCS+= $(addprefix $(call my-dir), rtld_start.S mdreloc.c)

# XXX Should not be in CPPFLAGS!
CFLAGS+=	-fpic

CFLAGS+=	-DELFSIZE=32
CFLAGS+=	-DRTLD_COMMON_CALL_IFUNC_REL

LDFLAGS+=	-Wl,-e,.rtld_start
