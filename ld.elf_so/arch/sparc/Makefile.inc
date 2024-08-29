#
# Makefile for Phoenix-RTOS ld.elf_so/arch
#
# Copyright 2024 Phoenix Systems
#

LDSO_ARCH_DIR := $(call my-dir)
LDSO_ARCH_PREFIX := $(LDSO_ARCH_DIR:$(LOCAL_DIR)%=%)

LOCAL_SRCS += $(addprefix $(LDSO_ARCH_PREFIX), rtld_start.S mdreloc.c)

LOCAL_CFLAGS += -DELFSIZE=32
LOCAL_CFLAGS += -DRTLD_COMMON_CALL_IFUNC_REL

LOCAL_LDFLAGS += $(LDFLAGS_PREFIX)-e,_rtld_start
LOCAL_LDFLAGS += $(TARGET_PIC_FLAG)

# FIXME
LOCAL_LDFLAGS += -z notext


# FIXME: improve tls implementation in Phoenix
LOCAL_CFLAGS += -D__HAVE_TLS_VARIANT_II
LOCAL_CFLAGS += -D__HAVE_COMMON___TLS_GET_ADDR
