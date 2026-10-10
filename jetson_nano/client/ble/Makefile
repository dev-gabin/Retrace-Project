CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic

TARGET := client_ble
SOURCES := client_ble.c ../common/tcp_client.c
PACKAGES := gio-2.0 glib-2.0

CPPFLAGS += -I../common

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SOURCES) ../common/tcp_client.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $$(pkg-config --cflags $(PACKAGES)) \
		-o $@ $(SOURCES) $$(pkg-config --libs $(PACKAGES))

clean:
	rm -f $(TARGET)
