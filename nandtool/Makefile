#
# Makefile for Phoenix-RTOS nandtool
#
# Copyright 2021 Phoenix Systems
#

NAME := nandtool
LOCAL_PATH = $(call my-dir)
LOCAL_SRCS := flashmng.c nandtool.c
LOCAL_INSTALL_PATH := /bin

ifneq (, $(findstring imx6ull, $(TARGET)))
  LOCAL_CFLAGS += -DHAS_BCB
  LOCAL_SRCS += bcb.c bch.c
endif

include $(binary.mk)
