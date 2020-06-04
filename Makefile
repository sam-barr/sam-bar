CFLAGS=-Wall -Wextra -Wpedantic \
	   $(shell pkg-config --cflags --libs xft) \
	   $(shell pkg-config --cflags --libs x11-xcb) \
	   $(shell pkg-config --cflags --libs xcb) \
	   -g -std=c99

all: main.c
	$(CC) $(CFLAGS) main.c -o sam-bar
