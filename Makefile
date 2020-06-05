CFLAGS=-Wall -Wextra -Wpedantic \
	   $(shell pkg-config --cflags --libs xcb) \
	   $(shell pkg-config --cflags --libs xcb-renderutil) \
	   $(shell pkg-config --cflags --libs xcb-xrm) \
	   $(shell pkg-config --cflags --libs fontconfig) \
	   -g

all: main.c
	$(CC) $(CFLAGS) main.c -o sam-bar
