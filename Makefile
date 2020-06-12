libs=xcb xcb-renderutil xcb-aux fontconfig

INSTALL_DIR = $(HOME)/.local/bin

CFLAGS=-Wall -Werror -Wextra -Wpedantic -Os -s \
	   $(shell for lib in $(libs); do pkg-config --cflags $$lib; done) \
	   -D_POSIX_C_SOURCE=200812L -DINSTALL_DIR=\"$(INSTALL_DIR)\" \
	   -std=c90

CLIBS = $(shell for lib in $(libs); do pkg-config --libs $$lib; done)

all: sam-bar

sam-bar: main.c fonts-for-xcb/xcbft/xcbft.c fonts-for-xcb/utf8_utils/utf8.c
	$(CC) $(CFLAGS) $(CLIBS) $^ -o $@

install: sam-bar listen-volume.sh
	install ./sam-bar $(INSTALL_DIR)/sam-bar
	install ./listen-volume.sh $(INSTALL_DIR)/listen-volume.sh

uninstall:
	rm $(INSTALL_DIR)/sam-bar $(INSTALL_DIR)/listen-volume.sh

clean:
	rm sam-bar
