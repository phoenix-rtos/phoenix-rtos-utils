#
# Makefile for Phoenix-RTOS ld.elf_so
#
# Copyright 2023 Phoenix Systems
#

LOCAL_DIR:=$(call my-dir)

NAME := ld.elf_so
LOCAL_SRCS := compat.c debug.c diagassert.c expand.c hash.c headers.c load.c map_object.c paths.c reloc.c rtld.c search.c symbol.c symver.c tls.c xmalloc.c xprintf.c
LIBS := libphoenix

LOCAL_CFLAGS += -D_PATH_RTLD=\"/bin/${NAME}\"

LOCAL_CFLAGS+= -DRTLD_LOADER
LOCAL_CFLAGS+= -D_RTLD_SOURCE
LOCAL_CFLAGS+= -DHAVE_INITFINI_ARRAY
# TODO: investigate further
# LOCAL_CFLAGS+=	-DGNU_RELRO

ifeq ($(TARGET_SUFF), ia32)
	COPTS:= -mno-3dnow -mno-mmx -mno-sse -mno-sse2 -mno-sse3
endif

COPTS+= -fvisibility=hidden -fomit-frame-pointer

# rtld.c and symbol.c use alloca, so disable SSP warnings.
COPTS.rtld.c+=   -Wno-stack-protector
COPTS.symbol.c+= -Wno-stack-protector

LOCAL_LDFLAGS+= -symbolic -shared
# FIXME
# LOCAL_LDFLAGS+= -nostartfiles -nodefaultlibs -lgcc
LOCAL_LDFLAGS+= -Wl,-static
LOCAL_LDFLAGS+= -Wl,--warn-shared-textrel
LOCAL_LDFLAGS+= -Wl,--no-undefined
LOCAL_LDFLAGS+=	-Wl,--version-script=${LOCAL_DIR}/symbols.map

ifeq (${TARGET_SUFF},ia32)
	include $(addprefix ${LOCAL_DIR}, arch/i386/Makefile.inc)
else ifeq (${TARGET_SUFF},armv7a)
	include $(addprefix ${LOCAL_DIR}, arch/arm/Makefile.inc)
else ifeq (${TARGET_SUFF},riscv64)
	include $(addprefix ${LOCAL_DIR}, arch/riscv64/Makefile.inc)
else
	$(error Unsupported architecture ${TARGET_SUFF})
endif

LANGUAGE_EXTENSIONS_OLD:=${LANGUAGE_EXTENSIONS}
LANGUAGE_EXTENSIONS+= %.S
include $(binary.mk)
LANGUAGE_EXTENSIONS:=${LANGUAGE_EXTENSIONS_OLD}

