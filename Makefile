all: client server

CFLAGS := 

ifdef RELEASE
	CFLAGS += -O2
else
	CFLAGS += -g
endif

client: client.o helpers.o ui.o
	gcc -o client client.o helpers.o ui.o

server: server.o helpers.o
	gcc -o server server.o helpers.o

client.o: client.c helpers.h types.h
	gcc -c $(CFLAGS) client.c

server.o: server.c helpers.h types.h
	gcc -c $(CFLAGS) server.c

helpers.o: helpers.c helpers.h
	gcc -c $(CFLAGS) helpers.c

ui.o: ui.c ui.h
	gcc -c $(CFLAGS) ui.c
	
clean:
	rm client.o server.o helpers.o ui.o client server
