CC = gcc
CFLAGS = -Wall -Wextra -std=gnu17 -O2
LDFLAGS = -pthread

all: nyuenc

nyuenc: nyuenc.c
	$(CC) $(CFLAGS) nyuenc.c -o nyuenc $(LDFLAGS)

clean:
	rm -f nyuenc