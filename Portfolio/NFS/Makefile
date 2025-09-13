CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = 
CLIENT_SRC = nfs_client.c
MANAGER_SRC = nfs_manager.c
CONSOLE_SRC = nfs_console.c
CLIENT_BIN = nfs_client
MANAGER_BIN = nfs_manager
CONSOLE_BIN = nfs_console
.PHONY: all clean
all: $(CLIENT_BIN) $(MANAGER_BIN) $(CONSOLE_BIN)
$(CLIENT_BIN): $(CLIENT_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(MANAGER_BIN): $(MANAGER_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(CONSOLE_BIN): $(CONSOLE_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
clean:
	rm -f $(CLIENT_BIN) $(MANAGER_BIN) $(CONSOLE_BIN)
