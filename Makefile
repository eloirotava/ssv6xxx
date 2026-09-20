# SPDX-License-Identifier: GPL-2.0-only
obj-$(CONFIG_SSV6XXX) += ssv6xxx.o
ssv6xxx-y := main.o
ssv6xxx-y += ssv6051/sdio.o ssv6051/hw.o ssv6051/mac.o ssv6051/tx.o \
	     ssv6051/rx.o ssv6051/rc.o ssv6051/ampdu.o ssv6051/ap.o
ssv6xxx-y += ssv6256/sdio.o ssv6256/hw.o ssv6256/mac.o ssv6256/tx.o \
	     ssv6256/rx.o ssv6256/phy.o ssv6256/ap.o

ifeq ($(KERNELRELEASE),)
# Out-of-tree build: make [KVER=<kernel version>] [KDIR=<kernel build dir>]
KVER ?= $(shell uname -r)
KDIR ?= /lib/modules/$(KVER)/build

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) CONFIG_SSV6XXX=m modules

install: all
	install -D -m 644 ssv6xxx.ko $(DESTDIR)/lib/modules/$(KVER)/updates/ssv6xxx.ko
	install -D -m 644 ssv6051/ssv6051-sw.bin $(DESTDIR)/lib/firmware/ssv/ssv6051-sw.bin
	install -D -m 644 ssv6256/ssv6x5x-sw.bin $(DESTDIR)/lib/firmware/ssv/ssv6x5x-sw.bin
	[ -n "$(DESTDIR)" ] || depmod -a $(KVER)

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean

.PHONY: all install clean
endif
