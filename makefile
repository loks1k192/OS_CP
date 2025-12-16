CC = gcc
CFLAGS = -Wall -Wextra -std=gnu11 -O2 -Iinclude
LIBS = -lpthread

SRCDIR = src
BINDIR = bin

SERVER_SRCS = $(SRCDIR)/server_main.c $(SRCDIR)/server_api.c
CLIENT_SRCS = $(SRCDIR)/client_main.c $(SRCDIR)/client_comm.c

.PHONY: all clean dirs

all: dirs $(BINDIR)/server $(BINDIR)/client

dirs:
	mkdir -p $(BINDIR)

$(BINDIR)/server: $(SERVER_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

$(BINDIR)/client: $(CLIENT_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

clean:
	rm -rf $(BINDIR)
