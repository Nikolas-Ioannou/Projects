CC = gcc
CFLAGS = -Wall -Wextra -O2


all: worker fss_manager fss_console

worker: worker.c
	$(CC) $(CFLAGS) -o worker worker.c

fss_manager: fss_manager.c
	$(CC) $(CFLAGS) -o fss_manager fss_manager.c

fss_console: fss_console.c
	$(CC) $(CFLAGS) -o fss_console fss_console.c

clean:
	rm -f worker fss_manager fss_console
