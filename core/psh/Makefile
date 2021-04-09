#
# Makefile for Phoenix-RTOS psh (Phoenix SHell)
#
# Copyright 2018, 2019, 2020, 2021 Phoenix Systems
#

ifdef PSH_DEFUSRPWDHASH
CFLAGS += -DPSH_DEFUSRPWDHASH=\"$(PSH_DEFUSRPWDHASH)\"
endif

PSH_ALLCOMMANDS = bind cat exec kill ls mem mkdir mount nc perf ping ps reboot runfile sync sysexec top touch
PSH_COMMANDS ?= $(PSH_ALLCOMMANDS)
OBJS = $(addsuffix .o,$(filter $(PSH_COMMANDS), $(PSH_ALLCOMMANDS)))

$(PREFIX_PROG)psh: $(addprefix $(PREFIX_O)core/psh/, psh.o psh-empties.o help.o $(OBJS))
	$(LINK)

all: $(PREFIX_PROG_STRIPPED)psh
