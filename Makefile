libs=xcb xcb-renderutil xcb-aux fontconfig

CFLAGS=-Wall -Werror -Wextra -Wpedantic -Os \
	   $(shell for lib in $(libs); do pkg-config --cflags $$lib; done) \
	   -D_POSIX_C_SOURCE=200812L \

CLIBS = $(shell for lib in $(libs); do pkg-config --libs $$lib; done)

INSTALL_DIR = $(HOME)/.local/bin

all: sam-bar
	
xcbft.o: fonts-for-xcb/xcbft/xcbft.c 
	$(CC) $(CFLAGS) -c $^ -o $@

utf8.o: fonts-for-xcb/utf8_utils/utf8.c
	$(CC) $(CFLAGS) -c $^ -o $@

sam-bar: main.c xcbft.o utf8.o
	$(CC) $(CFLAGS) $(CLIBS) -std=c90 -DINSTALL_DIR=\"$(INSTALL_DIR)\" $^ -o $@

install: sam-bar listen-volume.sh
	install ./sam-bar $(INSTALL_DIR)/sam-bar
	install ./listen-volume.sh $(INSTALL_DIR)/listen-volume.sh
	strip $(HOME)/.local/bin/sam-bar

uninstall:
	rm $(INSTALL_DIR)/sam-bar $(INSTALL_DIR)/listen-volume.sh

clean:
	rm *.o sam-bar
