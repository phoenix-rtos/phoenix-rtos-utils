#
# Makefile for Phoenix-RTOS psh (Phoenix SHell)
#
# Copyright 2018, 2019, 2020, 2021 Phoenix Systems
#

OBJS = $(filter-out psh psh-empties, $(subst .c,,$(subst ./core/psh/,, $(shell find ./core/psh/ -name '*.c'))))
PSH_COMMANDS ?= $(OBJS)
OBJ_FEATURED = $(addsuffix .o,$(filter $(PSH_COMMANDS),$(OBJS)))

$(PREFIX_PROG)psh: $(addprefix $(PREFIX_O)core/psh/, psh.o psh-empties.o $(OBJ_FEATURED))
	$(LINK)

all: $(PREFIX_PROG_STRIPPED)psh
