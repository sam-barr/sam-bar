CFLAGS=-Wall -Wextra -Wpedantic \
	   $(shell pkg-config --cflags --libs xcb) \
	   $(shell pkg-config --cflags --libs xcb-renderutil) \
	   $(shell pkg-config --cflags --libs xcb-xrm) \
	   $(shell pkg-config --cflags --libs xcb-aux) \
	   $(shell pkg-config --cflags --libs fontconfig) \
	   -O2

all: sam-bar

sam-bar: main.c
	$(CC) $(CFLAGS) main.c -o sam-bar

install: sam-bar
	install ./sam-bar $(HOME)/.local/bin/sam-bar
	strip $(HOME)/.local/bin/sam-bar
