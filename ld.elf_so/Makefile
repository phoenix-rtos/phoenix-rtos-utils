#
# Makefile for Phoenix-RTOS ld.elf_so
#
# Copyright 2023-2024 Phoenix Systems
#

LOCAL_DIR:=$(call my-dir)

NAME := ld.elf_so
LOCAL_SRCS := debug.c expand.c hash.c headers.c load.c map_object.c paths.c reloc.c rtld.c search.c symbol.c symver.c tls.c xmalloc.c xprintf.c
LIBS :=

LOCAL_INSTALL_PATH := /usr/libexec

LOCAL_CFLAGS += -D_PATH_RTLD=\"$(LOCAL_INSTALL_PATH)/$(NAME)\"
LOCAL_CFLAGS += -DRTLD_LOADER
LOCAL_CFLAGS += -D_RTLD_SOURCE
LOCAL_CFLAGS += -DHAVE_INITFINI_ARRAY
LOCAL_CFLAGS += -DGNU_RELRO
LOCAL_CFLAGS += -DRTLD_DEFAULT_LIBRARY_PATH=\"$(DEFAULT_INSTALL_PATH_SO)\"

# Debug flags
# LOCAL_CFLAGS += -DDEBUG
# LOCAL_CFLAGS += -DRTLD_DEBUG
# LOCAL_CFLAGS += -DRTLD_DEBUG_RELOC

# TODO: devise if it should be removed from here
ifeq ($(TARGET_SUFF), ia32)
	LOCAL_CFLAGS += -mno-3dnow -mno-mmx -mno-sse -mno-sse2 -mno-sse3
endif

LOCAL_CFLAGS += -fvisibility=hidden -fomit-frame-pointer

# rtld.c and symbol.c use alloca, so disable SSP warnings.
# FIXME add support for file specific CFLAGS, now it works as stack-protector is not used by default.
CFLAGS.$(LOCAL_DIR)rtld.c   += -Wno-stack-protector
CFLAGS.$(LOCAL_DIR)symbol.c += -Wno-stack-protector

LOCAL_LDFLAGS += -symbolic -shared
LOCAL_LDFLAGS += -nostartfiles -nodefaultlibs
LOCAL_LDFLAGS += $(LDFLAGS_PREFIX)-static
LOCAL_LDFLAGS += -z text
LOCAL_LDFLAGS += $(LDFLAGS_PREFIX)--no-undefined
LOCAL_LDFLAGS += $(LDFLAGS_PREFIX)--version-script=$(LOCAL_DIR)/symbols.map

LOCAL_LDLIBS := -lphoenix -lgcc

ifeq ($(TARGET_SUFF),ia32)
	include $(addprefix $(LOCAL_DIR), arch/i386/Makefile.inc)
else ifeq ($(findstring riscv,$(TARGET_SUFF)),riscv)
	include $(addprefix $(LOCAL_DIR), arch/riscv/Makefile.inc)
else ifeq ($(findstring arm,$(TARGET_SUFF)),arm)
	include $(addprefix $(LOCAL_DIR), arch/arm/Makefile.inc)
else ifeq ($(findstring sparc,$(TARGET_SUFF)),sparc)
	include $(addprefix $(LOCAL_DIR), arch/sparc/Makefile.inc)
endif

LANGUAGE_EXTENSIONS_OLD := $(LANGUAGE_EXTENSIONS)
LANGUAGE_EXTENSIONS += %.S

# FDPIC symbol hiding overwrite
LDFLAGS_OLD := $(LDFLAGS)
LDFLAGS := $(filter-out $(LDFLAGS_PREFIX)--version-script="$(hide.map)" , $(LDFLAGS))

TARGET_STATIC_FLAG_PREV := $(TARGET_STATIC_FLAG)
TARGET_STATIC_FLAG := -static

include $(binary.mk)

TARGET_STATIC_FLAG := $(TARGET_STATIC_FLAG_PREV)

LDFLAGS := $(LDFLAGS_OLD)

LANGUAGE_EXTENSIONS := $(LANGUAGE_EXTENSIONS_OLD)


rtld: ld.elf_so librtld_stubs
rtld-install: ld.elf_so-install librtld_stubs-install
rtld-clean: ld.elf_so-clean librtld_stubs-clean