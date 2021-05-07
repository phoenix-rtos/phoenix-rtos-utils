#
# Makefile for Phoenix-RTOS psh (Phoenix SHell)
#
# Copyright 2018, 2019, 2020, 2021 Phoenix Systems
#

PSH_DIR = core/psh/

PSH_DEFUSRPWDHASH ?= ""
CFLAGS += -DPSH_DEFUSRPWDHASH=\"$(PSH_DEFUSRPWDHASH)\"
PSH_SYSEXECWL ?= ""
CFLAGS += -DPSH_SYSEXECWL='"$(PSH_SYSEXECWL)"'

PSH_ALLCOMMANDS = bind cat exec kill ls mem mkdir mount nc perf ping ps reboot runfile sync sysexec top touch
PSH_COMMANDS ?= $(PSH_ALLCOMMANDS)
PSH_APPLETS = pshapp help login $(filter $(PSH_ALLCOMMANDS), $(PSH_COMMANDS))

PSH_OBJS = $(PSH_DIR)psh.o
PSH_OBJS += $(foreach app, $(PSH_APPLETS), $(patsubst %.c, %.o, $(wildcard $(PSH_DIR)$(app)/*.c)))

$(PREFIX_PROG)psh: $(addprefix $(PREFIX_O), $(PSH_OBJS))
	$(LINK)

all: $(PREFIX_PROG_STRIPPED)psh