#
# Makefile for Phoenix-RTOS nandtool
#
# Copyright 2021 Phoenix Systems
#

NAME := nandtool
LOCAL_PATH = $(call my-dir)
SRCS := $(wildcard $(LOCAL_PATH)*.c)
LOCAL_INSTALL_PATH := /bin

ifneq (, $(findstring imx6ull, $(TARGET)))
  LOCAL_CFLAGS += -DHAS_BCB
endif

include $(binary.mk)
