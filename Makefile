
.PHONY:all
all:muxconn.so

CC=gcc
ifeq ($(debug),1)
	DEBUG_CFLAGS=-g -O0
else
	DEBUG_CFLAGS=-DNDEBUG -O3
endif

CFLAGS = -I./src -I./include $(DEBUG_CFLAGS) -fPIC
LDFLAGS = -levent -lm

TARGET_DEPS =			\
	src/mux.o			\
	src/server.o		\
	src/client.o		\
	src/socket.o		\
	src/hashtable.o		\
	src/utils.o			\
	src/version.o
	
.PHONY: debug
debug:
	@make debug=1 --no-print-directory

muxconn.so:$(TARGET_DEPS)
	gcc $^ -fPIC -shared -o muxconn.so

install:muxconn.so
	cp -f muxconn.so /usr/lib
	cp -rf ./include/* /usr/include

.PHONY: clean test

test:
	cd test && make

clean:
	rm -f $(TARGET_DEPS) 