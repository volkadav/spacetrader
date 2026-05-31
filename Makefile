CC ?= gcc
CFLAGS ?= -std=c99 -Wall -Wextra -Wpedantic -O2
NCURSES_CFLAGS := $(shell pkg-config --cflags ncurses 2>/dev/null)
NCURSES_LIBS := $(shell pkg-config --libs ncurses 2>/dev/null)
ifeq ($(strip $(NCURSES_LIBS)),)
NCURSES_LIBS := -lncurses
endif

SRC := main.c util.c world.c player.c market.c combat.c encounter.c prospect.c gamble.c save.c ui.c
OBJ := $(SRC:.c=.o)

all: spacetrader

spacetrader: $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $@ $(NCURSES_LIBS)

%.o: %.c game.h
	$(CC) $(CFLAGS) $(NCURSES_CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) spacetrader

.PHONY: all clean
