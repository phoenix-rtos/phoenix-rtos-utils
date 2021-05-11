#
# Makefile for Phoenix-RTOS psh (Phoenix SHell)
#
# Copyright 2018, 2019, 2020, 2021 Phoenix Systems
#

NAME := psh
LOCAL_SRCS := psh.c
LOCAL_PATH = $(call my-dir)

PSH_DEFUSRPWDHASH ?= ""
CFLAGS += -DPSH_DEFUSRPWDHASH=\"$(PSH_DEFUSRPWDHASH)\"
PSH_SYSEXECWL ?= ""
CFLAGS += -DPSH_SYSEXECWL='"$(PSH_SYSEXECWL)"'

PSH_ALLCOMMANDS = bind cat exec kill ls mem mkdir mount nc perf ping ps reboot runfile sync sysexec top touch
PSH_COMMANDS ?= $(PSH_ALLCOMMANDS)
PSH_APPLETS = pshapp help login $(filter $(PSH_ALLCOMMANDS), $(PSH_COMMANDS))

SRCS := $(foreach app, $(PSH_APPLETS), $(wildcard $(LOCAL_DIR)$(app)/*.c))

include $(binary.mk)

