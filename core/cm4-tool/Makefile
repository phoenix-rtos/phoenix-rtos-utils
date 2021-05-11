#
# Makefile for Phoenix-RTOS imxrt117x-cm4 tool
#
# Copyright 2018, 2019 Phoenix Systems
#

$(PREFIX_O)core/cm4-tool/tool.o: $(PREFIX_H)imxrt117x-cm4.h

$(PREFIX_PROG)cm4-tool: $(PREFIX_O)core/cm4-tool/tool.o
	$(LINK)

all: $(PREFIX_PROG_STRIPPED)cm4-tool
