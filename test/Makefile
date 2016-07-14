BIN=client_test server_test utils_test socket_test bench_server bench_client
.PHONY:all
all:$(BIN)

CC=gcc
DEBUG_CFLAGS=-g -O0

CFLAGS = -I../include -I../src $(DEBUG_CFLAGS)
LDFLAGS = -lm /usr/local/lib/libevent.a -ldl -lrt 

TARGET_DEPS =			\
	../src/mux.o		\
	../src/server.o		\
	../src/client.o		\
	../src/socket.o		\
	../src/hashtable.o	\
	../src/utils.o		\
	../src/version.o

.PHONY: clean

client_test: $(TARGET_DEPS) client_test.c
	$(CC) $^ -o $@ $(LDFLAGS) $(CFLAGS)

server_test: $(TARGET_DEPS) server_test.c
	$(CC) $^ -o $@ $(LDFLAGS) $(CFLAGS)

bench_client: $(TARGET_DEPS) bench_client.c
	$(CC) $^ -o $@ $(LDFLAGS) $(CFLAGS)

bench_server: $(TARGET_DEPS) bench_server.c
	$(CC) $^ -o $@ $(LDFLAGS) $(CFLAGS)

socket_test: $(TARGET_DEPS) socket_test.c
	$(CC) $^ -o $@ $(LDFLAGS) $(CFLAGS)
	
utils_test: $(TARGET_DEPS) utils_test.c
	$(CC) $^ -o $@ $(LDFLAGS) $(CFLAGS)
clean:
	rm -f $(TARGET_DEPS) $(BIN)
