#
# Makefile for Phoenix-RTOS psd (Phoenix Serial Downloader)
#
# Copyright 2018, 2019 Phoenix Systems
#

$(PREFIX_PROG)psd: $(addprefix $(PREFIX_O)psd/common/, hid.o) $(PREFIX_A)libusb.a 

ifneq (, $(findstring imx6ull, $(TARGET)))
CFLAGS += -DTARGET_IMX6ULL 

$(PREFIX_PROG)psd: $(addprefix $(PREFIX_O)psd/imx6ull/, bch.o bcb.o flashmng.o psd.o) $(PREFIX_A)libflashdrv.a
	$(LINK)

else ifneq (, $(findstring imxrt, $(TARGET)))
CFLAGS += -DTARGET_IMXRT

$(PREFIX_PROG)psd:  $(addprefix $(PREFIX_O)psd/imxrt/, psd.o) 
	$(LINK)
endif

$(PREFIX_PROG)psd-old: $(PREFIX_O)psd/psd-old.o $(PREFIX_A)libusb.a
	$(LINK)

all: $(PREFIX_PROG_STRIPPED)psd-old $(PREFIX_PROG_STRIPPED)psd

