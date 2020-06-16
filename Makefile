libs=xcb xcb-renderutil xcb-aux fontconfig

INSTALL_DIR=$(HOME)/.local/bin

CFLAGS=-Wall -Werror -Wextra -Wpedantic -std=c90 \
	   $(shell for lib in $(libs); do pkg-config --cflags $$lib; done) \
	   -D_POSIX_C_SOURCE=200812L

CLIBS=$(shell for lib in $(libs); do pkg-config --libs $$lib; done)

OPT=-Os -s -flto

DEBUG=-Og -g -DDEBUG

CSOURCE=main.c fonts-for-xcb/xcbft/xcbft.c fonts-for-xcb/utf8_utils/utf8.c

.PHONY: all
all: sam-bar debug

debug: $(CSOURCE)
	$(CC) $(CFLAGS) $(CLIBS) $(DEBUG) $^ -o $@

sam-bar: $(CSOURCE)
	$(CC) $(CFLAGS) $(CLIBS) $(OPT) $^ -o $@

.PHONY: install
install: sam-bar
	install ./sam-bar $(INSTALL_DIR)/sam-bar

.PHONY: uninstall
uninstall:
	rm $(INSTALL_DIR)/sam-bar $(INSTALL_DIR)/listen-volume.sh

.PHONY: clean
clean:
	rm sam-bar debug
