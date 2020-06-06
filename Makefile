libs=xcb xcb-renderutil xcb-aux fontconfig

CFLAGS=-Wall -Wextra -Wpedantic -O2 \
	   $(shell for lib in $(libs); do pkg-config --cflags --libs $$lib; done) \

all: sam-bar

sam-bar: main.c
	$(CC) $(CFLAGS) main.c -o sam-bar

install: sam-bar
	install ./sam-bar $(HOME)/.local/bin/sam-bar
	strip $(HOME)/.local/bin/sam-bar
