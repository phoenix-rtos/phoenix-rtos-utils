#
# Makefile for Phoenix-RTOS psh (Phoenix SHell)
#
# Copyright 2018, 2019, 2020 Phoenix Systems
#

$(PREFIX_PROG)psh: $(addprefix $(PREFIX_O)core/psh/, cat.o fs.o kill.o ls.o mem.o perf.o ps.o psh.o reboot.o top.o)
	$(LINK)

all: $(PREFIX_PROG_STRIPPED)psh
