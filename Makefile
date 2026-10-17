CC      ?= gcc
VERSION := $(shell cat VERSION)
GITREV  := $(shell git rev-parse --short HEAD 2>/dev/null || echo nogit)
BUILD   := $(shell date -u +%Y-%m-%dT%H:%M:%SZ)
CFLAGS  ?= -O2 -Wall -Wextra -std=gnu11 -D_GNU_SOURCE -D_XOPEN_SOURCE_EXTENDED
CPPFLAGS += -DBUILD_VERSION=\"$(VERSION)\" -DBUILD_REV=\"$(GITREV)\" -DBUILD_DATE=\"$(BUILD)\"
LDFLAGS ?= -static -s
LDLIBS  ?= -lncursesw -ltinfo

SRC = main.c common.c i18n.c server.c client.c bot.c viewer.c
OBJ = $(SRC:.c=.o)

battleship: $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

%.o: %.c common.h i18n.h VERSION
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

clean:
	rm -f battleship $(OBJ)

.PHONY: clean
