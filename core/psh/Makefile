#
# Makefile for Phoenix-RTOS psh (Phoenix SHell)
#
# Copyright 2018, 2019, 2020, 2021 Phoenix Systems
#

ifdef DEFUSR_PASSWD
CFLAGS += -DDEFUSR_PASSWD=\"$(DEFUSR_PASSWD)\"
endif

OBJS = bind cat exec kill ls mem mkdir mount perf ps reboot runfile sync sysexec top touch
PSH_COMMANDS ?= $(OBJS)
OBJ_FEATURED = $(addsuffix .o,$(filter $(PSH_COMMANDS), $(OBJS)))

$(PREFIX_PROG)psh: $(addprefix $(PREFIX_O)core/psh/, psh.o psh-empties.o help.o $(OBJ_FEATURED))
	$(LINK)

all: $(PREFIX_PROG_STRIPPED)psh
