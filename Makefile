CC=gcc
CFLAGS=-Wall -Wextra -std=c11 -O2 -march=native
LDFLAGS=-lmicrohttpd -lcjson

image_server: image_server.c
	$(CC) $(CFLAGS) image_server.c -o image_server $(LDFLAGS)
