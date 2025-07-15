CC = gcc
CFLAGS = 
LDFLAGS = -lssl -lcrypto

websocketserver: websocketserver.c
	$(CC) $(CFLAGS) websocketserver.c -o websocketserver $(LDFLAGS)

clean:
	rm -f websocketserver
