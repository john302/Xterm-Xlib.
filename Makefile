CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2 -std=c99 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -D_BSD_SOURCE
INCLUDES = -Isrc $(shell pkg-config --cflags xft x11 fontconfig)
LIBS = $(shell pkg-config --libs xft x11 fontconfig) -lutil

SRCS = src/main.c src/term.c src/selection.c src/pty_mgr.c src/x11_ui.c
OBJDIR = obj
OBJS = $(patsubst src/%.c, $(OBJDIR)/%.o, $(SRCS))
TARGET = bin/eterm-xlib

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p bin
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LIBS)
	@echo "Build successful: $(TARGET)"

$(OBJDIR)/%.o: src/%.c
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/eterm-xlib

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/eterm-xlib

clean:
	rm -rf $(OBJDIR) bin
	rm -f src/*.o $(TARGET)

.PHONY: all clean install uninstall
