CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic

TARGET := client_bluetooth
SOURCES := client_bluetooth.c ../common/tcp_client.c

CPPFLAGS += -I../common
LDLIBS += -lbluetooth

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SOURCES) ../common/tcp_client.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(SOURCES) $(LDLIBS)

clean:
	rm -f $(TARGET)
