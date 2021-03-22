#
# Makefile for Phoenix-RTOS psh (Phoenix SHell)
#
# Copyright 2018, 2019, 2020, 2021 Phoenix Systems
#

PSH_COMMANDS ?= bind cat exec help kill ls mem mkdir mount perf ps reboot runfile sync sysexec top touch
OBJS = bind cat exec help kill ls mem mkdir mount perf ps reboot runfile sync sysexec top touch
OBJ_FEATURED=$(addsuffix -empty.o,$(filter-out $(PSH_COMMANDS),$(OBJS))) $(addsuffix .o,$(filter $(PSH_COMMANDS),$(OBJS)))

$(info $(OBJ_FEATURED))

$(PREFIX_PROG)psh: $(addprefix $(PREFIX_O)core/psh/, psh.o $(OBJ_FEATURED))
	$(LINK)

all: $(PREFIX_PROG_STRIPPED)psh
