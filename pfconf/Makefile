#
# Makefile for Phoenix-RTOS pfconf
#
# Copyright 2018, 2019 Phoenix Systems
#

CFLAGS+=-I../phoenix-rtos-lwip/lib-lwip/src/include -I../phoenix-rtos-lwip/include

$(PREFIX_PROG)pfconf: $(PREFIX_O)pfconf/pfconf.o $(PREFIX_A)libsystem.a
	$(LINK)

pfconf: $(PREFIX_PROG_STRIPPED)pfconf
