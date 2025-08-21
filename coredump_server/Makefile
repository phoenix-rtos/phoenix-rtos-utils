#
# Makefile for Phoenix-RTOS coredump_server
#
# Copyright 2025 Phoenix Systems
#


NAME := coredump_server
LOCAL_SRCS := coredump_srv.c encoding.c settings.c kernel_comm.c hal/$(TARGET_SUFF)/cpu.c
LOCAL_CFLAGS := -Icoredump_server -Icoredump_server/hal/$(TARGET_SUFF)
LOCAL_LDFLAGS := -Wl,-z,stack-size=4096


ifneq (, $(filter imxrt117x imxrt106x imxrt105x, $(TARGET_SUBFAMILY)))
	LOCAL_CFLAGS += -DCPU_IMXRT
endif

ifeq ($(COREDUMP_DISABLE), 1)
	CPPFLAGS += -DCOREDUMP_DISABLE
endif

include $(binary.mk)
