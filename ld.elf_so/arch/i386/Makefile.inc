#	$NetBSD: Makefile.inc,v 1.15 2018/04/03 21:10:27 joerg Exp $

LDSO_ARCH_DIR := $(call my-dir)
LDSO_ARCH_PREFIX := $(LDSO_ARCH_DIR:$(LOCAL_DIR)%=%)

LOCAL_SRCS += $(addprefix $(LDSO_ARCH_PREFIX), rtld_start.S mdreloc.c)

LOCAL_CFLAGS += -fpic
LOCAL_CFLAGS += -DELFSIZE=32
LOCAL_CFLAGS += -DRTLD_COMMON_CALL_IFUNC_REL

LOCAL_LDFLAGS += $(LDFLAGS_PREFIX)-e,.rtld_start
LOCAL_LDFLAGS += -fpic
