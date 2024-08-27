#
# Makefile for Phoenix-RTOS ld.elf_so/arch
#
# Copyright 2024 Phoenix Systems
#

LDSO_ARCH_DIR := $(call my-dir)
LDSO_ARCH_PREFIX := $(LDSO_ARCH_DIR:$(LOCAL_DIR)%=%)

LOCAL_SRCS += $(addprefix $(LDSO_ARCH_PREFIX), rtld_start.S mdreloc.c)

LOCAL_CFLAGS += $(TARGET_PIC_FLAG)
LOCAL_CFLAGS += -DELFSIZE=32
LOCAL_CFLAGS += -DRTLD_COMMON_CALL_IFUNC_REL

LOCAL_LDFLAGS += $(LDFLAGS_PREFIX)-e,_rtld_start
LOCAL_LDFLAGS += $(TARGET_PIC_FLAG)

# We always use eabi
LOCAL_SRCS += $(addprefix $(LDSO_ARCH_PREFIX), find_exidx.c)

# FIXME: improve tls implementation in Phoenix
LOCAL_CFLAGS += -D__HAVE_TLS_VARIANT_I
LOCAL_CFLAGS += -D__HAVE_COMMON___TLS_GET_ADDR
