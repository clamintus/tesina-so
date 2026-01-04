all: client server

CC	:= gcc
CFLAGS  := -pthread
LDFLAGS := -pthread -static

ifdef RELEASE
	CFLAGS += -O2
else
	CFLAGS += -g
endif

ifdef ANDROID__BUILD_VERSION_SDK
	CFLAGS += -DPOSIX_MUTEX
else ifdef POSIX_MUTEX
	CFLAGS += -DPOSIX_MUTEX
endif

ifdef DEBUG
	CFLAGS += -DDEBUG
endif
ifeq ($(DEBUG),2)
	CFLAGS += -fsanitize=address,undefined
	LDFLAGS += -fsanitize=address,undefined
endif

client: client.o helpers.o ui.o
	$(CC) $(LDFLAGS) -o client client.o helpers.o ui.o

server: server.o helpers.o
	$(CC) $(LDFLAGS) -o server server.o helpers.o

client.o: client.c helpers.h types.h
	$(CC) -c $(CFLAGS) client.c

server.o: server.c helpers.h types.h
	$(CC) -c $(CFLAGS) server.c

helpers.o: helpers.c helpers.h
	$(CC) -c $(CFLAGS) helpers.c

ui.o: ui.c ui.h
	$(CC) -c $(CFLAGS) ui.c
	
clean:
	rm client.o server.o helpers.o ui.o client server
