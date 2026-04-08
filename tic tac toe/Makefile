CC = gcc
CFLAGS = -Wall -pthread -g
LDFLAGS = -lrt

all: server client

server: server.c
	$(CC) $(CFLAGS) -o server server.c $(LDFLAGS)

client: client.c
	$(CC) $(CFLAGS) -o client client.c

clean:
	rm -f server client game.log scores.txt