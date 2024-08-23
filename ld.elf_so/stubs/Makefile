#
# Makefile for Phoenix-RTOS librtld_stubs
#
# Copyright 2024 Phoenix Systems
#

# Make library with stubs of non POSIX functions.
# This is required as this linker doesn't allow to add itself to NEEDED libs.
# To resolve a symbol from linker in dynamic binary a weak symbol is required.
NAME := librtld_stubs
LOCAL_SRCS := librtld_stubs.c
include $(shared-lib.mk)
