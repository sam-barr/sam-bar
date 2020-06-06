libs=xcb xcb-renderutil xcb-aux fontconfig

CFLAGS=-Wall -Werror -Wextra -Wpedantic -O2 \
	   $(shell for lib in $(libs); do pkg-config --cflags $$lib; done)

CLIBS = $(shell for lib in $(libs); do pkg-config --libs $$lib; done)

all: sam-bar
	
xcbft.o: fonts-for-xcb/xcbft/xcbft.c 
	$(CC) $(CFLAGS) -c $^ -o $@

utf8.o: fonts-for-xcb/utf8_utils/utf8.c
	$(CC) $(CFLAGS) -c $^ -o $@

sam-bar: main.c xcbft.o utf8.o
	$(CC) $(CFLAGS) $(CLIBS) -std=c90 $^ -o $@

install: sam-bar
	install ./sam-bar $(HOME)/.local/bin/sam-bar
	strip $(HOME)/.local/bin/sam-bar

clean:
	rm *.o sam-bar
