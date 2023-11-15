#
# Makefile for Phoenix-RTOS psh (Phoenix SHell)
#
# Copyright 2018-2023 Phoenix Systems
#

NAME := psh
LOCAL_SRCS := psh.c
LOCAL_PATH = $(call my-dir)

### ENV-based configuration ###
# password hash for 'defusr' user
PSH_DEFUSRPWDHASH ?=
# allowed sysexec whitelist (empty == all allowed)
PSH_SYSEXECWL ?=

LOCAL_CFLAGS := -DPSH_DEFUSRPWDHASH=\"$(PSH_DEFUSRPWDHASH)\"
LOCAL_CFLAGS += -DPSH_SYSEXECWL='"$(PSH_SYSEXECWL)"'
LOCAL_LDFLAGS := -z stack-size=4096 -z noexecstack

# TODO: search for dirs?
PSH_ALLCOMMANDS := bind cat cd cp date dd df dmesg echo edit exec hm ifconfig kill \
ln ls mem mkdir mount nc nslookup ntpclient perf ping pm printenv ps pwd reboot rm \
rmdir runfile sync sysexec top touch tty umount uptime wget
PSH_COMMANDS ?= $(PSH_ALLCOMMANDS)
PSH_INTERNAL_APPLETS := pshapp help $(filter $(PSH_ALLCOMMANDS), $(PSH_COMMANDS))

SRCS := $(foreach app, $(PSH_INTERNAL_APPLETS), $(wildcard $(LOCAL_PATH)$(app)/*.c))

# Header for project-specific applets
HEADERS:= $(LOCAL_PATH)/psh.h

# Project-specific applets .a files and their dependencies
LIBS := $(PSH_PROJECT_DEPS)

# Applets to create links:
PSH_LINK_APPLETS := $(PSH_COMMANDS) pshlogin
# custom installation - automatically create rootfs hardlinks for all applets
$(NAME)-install:
	$(SIL)for applet in $(PSH_LINK_APPLETS); do \
		echo "LINK /bin/{psh -> $$applet}"; \
		ln -f "$$PREFIX_ROOTFS/bin/psh" "$$PREFIX_ROOTFS/bin/$$applet"; \
	done

include $(binary.mk)

