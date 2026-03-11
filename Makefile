PREFIX ?= /usr/local

CFLAGS	+= -O2
CFLAGS	+= -Wall -Wextra -Wpedantic

LDFLAGS	+= -framework IOKit -framework CoreFoundation

.PHONY: all
all: sn2bsd

sn2bsd: main.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

.PHONY: clean
clean:
	rm -f sn2bsd

.PHONY: install
install: sn2bsd
	install sn2bsd $(PREFIX)/bin/
