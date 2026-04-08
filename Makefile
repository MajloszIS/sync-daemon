CC = gcc
CFLAGS = -Wall -Wextra

daemon: demon.c
	$(CC) $(CFLAGS) -o daemon demon.c

clean:
	rm -f daemon
