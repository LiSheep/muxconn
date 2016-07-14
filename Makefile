
.PHONY:all
all:muxconn

CC=gcc
ifeq ($(debug),1)
	DEBUG_CFLAGS=-g -O0
else
	DEBUG_CFLAGS=-DNDEBUG -O3
endif

CFLAGS = -I./src -I./include $(DEBUG_CFLAGS)
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

muxconn:$(TARGET_DEPS)

.PHONY: clean test

test:
	cd test && make

clean:
	rm -f $(TARGET_DEPS) 