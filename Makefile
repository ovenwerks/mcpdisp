#
# mcpdisp - A mackie control surface display emulator
#
# Copyright (c) 2015 Len Ovens <len at ovenwerks dot net>
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 as published by
# the Free Software Foundation.
#

# prefix := /usr/local
prefix := /usr
bindir := $(prefix)/bin
#sysconfdir := /etc

# Yes, I am lazy...
#VER := $(shell head -n 1 NEWS | cut -d : -f 1)



DEBUG :=
CFLAGS := -O2 -Wall $(DEBUG)
#CPPFLAGS := -DVERSION=\"$(VER)\" -DCONFIG=\"$(sysconfdir)/actkbd.conf\"



all: mcpdisp

mcpdisp: mcpdisp.o -ljack

install: all
	install -D -m755 mcpdisp $(bindir)/mcpdisp
	install -D -m755 mcpdisp.sh $(bindir)/mcpdisp.sh
	install -D -m644 mcpdisp.desktop $(prefix)/share/applications/mcpdisp.desktop
	install -D -m644 mcpdisp.svg $(prefix)/share/icons/hicolor/scalable/apps/mcpdisp.svg
	

clean:
	rm -f mcpdisp *.o
