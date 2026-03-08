CC ?= gcc
CFLAGS ?= -O2 -Wall
LDFLAGS ?=
LDLIBS = -lm

PREFIX ?= /usr
BINDIR ?= $(PREFIX)/bin
UNITDIR ?= $(PREFIX)/lib/systemd/system

nv-bright: nv-bright.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

install: nv-bright
	install -Dm755 nv-bright $(DESTDIR)$(BINDIR)/nv-bright
	install -Dm755 nv-bright-wrapper $(DESTDIR)$(BINDIR)/nv-bright-wrapper
	install -Dm644 nv-bright.service $(DESTDIR)$(UNITDIR)/nv-bright.service

clean:
	rm -f nv-bright

.PHONY: install clean
