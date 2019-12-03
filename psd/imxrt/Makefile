#
# Makefile for Phoenix-RTOS psd (Phoenix Serial Downloader)
#
# Copyright 2018, 2019 Phoenix Systems
#

$(PREFIX_PROG)psd: $(addprefix $(PREFIX_O)psd/common/, hid.o) $(PREFIX_A)libusbc.a $(PREFIX_A)libflashdrv.a 

$(PREFIX_PROG)psd:  $(addprefix $(PREFIX_O)psd/imxrt/, psd.o) 
	$(LINK)

all: $(PREFIX_PROG_STRIPPED)psd
