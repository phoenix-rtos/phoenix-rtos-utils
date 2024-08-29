#
# Makefile for Phoenix-RTOS ld.elf_so/arch
#
# Copyright 2024 Phoenix Systems
#

LDSO_ARCH_DIR := $(call my-dir)
LDSO_ARCH_PREFIX := $(LDSO_ARCH_DIR:$(LOCAL_DIR)%=%)

LOCAL_SRCS += $(addprefix $(LDSO_ARCH_PREFIX), rtld_start.S mdreloc.c)

LOCAL_CFLAGS += $(TARGET_PIC_FLAG)

ifeq ($(findstring riscv64,$(TARGET_SUFF)),riscv64)
LOCAL_CFLAGS +=	-DELFSIZE=64
else
LOCAL_CFLAGS +=	-DELFSIZE=32
endif

LOCAL_LDFLAGS += $(LDFLAGS_PREFIX)-e,_rtld_start

# FIXME: improve tls implementation in Phoenix
LOCAL_CFLAGS += -DTLS_DTV_OFFSET=0x800
LOCAL_CFLAGS += -D__HAVE_TLS_VARIANT_I
LOCAL_CFLAGS += -D__HAVE_COMMON___TLS_GET_ADDR

LOCAL_CFLAGS += -D__HAVE___LWP_GETTCB_FAST
LOCAL_CFLAGS += -D__HAVE___LWP_SETTCB
