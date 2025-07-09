all: client server

client: client.o helpers.o
	gcc -o client client.o helpers.o

server: server.o helpers.o
	gcc -o server server.o helpers.o

client.o: client.c helpers.h types.h
	gcc -c -g client.c

server.o: server.c helpers.h types.h
	gcc -c -g server.c

helpers.o: helpers.c helpers.h
	gcc -c -g helpers.c
	
clean:
	rm client.o server.o helpers.o client server
