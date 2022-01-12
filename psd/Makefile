#
# Makefile for Phoenix-RTOS psd (Phoenix Serial Downloader)
#
# Copyright 2018, 2019 Phoenix Systems
#

NAME := psd
LOCAL_PATH = $(call my-dir)
LOCAL_SRCS := common/sdp.c
LIBS := libusb libusbclient
LOCAL_INSTALL_PATH := /sbin

ifneq (, $(findstring imxrt, $(TARGET)))
  LOCAL_SRCS += imxrt/psd.c
else ifneq (, $(findstring imx6ull, $(TARGET)))
  SRCS := $(filter-out %/psd-old.c, $(wildcard $(LOCAL_PATH)imx6ull/*.c))
else
$(NAME): ; $(error $(NAME) for $(TARGET) not implemented)
endif

include $(binary.mk)

NAME := psd-old
LOCAL_SRCS := imx6ull/psd-old.c
LIBS := libusbclient
LOCAL_INSTALL_PATH := /sbin
include $(binary.mk)
