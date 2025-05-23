#
# Makefile for common benchmark functions library
#
# Copyright 2025 Phoenix Systems
#
# %LICENSE%
#

NAME := bench_common
LOCAL_SRCS := bench_common.c
LOCAL_HEADERS := bench_common.h bench_plat.h

ifeq ($(TARGET_SUBFAMILY),gr740)
  LOCAL_SRCS += bench_plat_gr740.c
else ifeq ($(TARGET_SUBFAMILY),tda4vm)
  LOCAL_SRCS += bench_plat_tda4vm.c
endif

include $(static-lib.mk)
