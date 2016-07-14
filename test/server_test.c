#include "mux/mux.h"
#include "mux/socket.h"

#include <event2/event.h>
#include <string.h>
#include <assert.h>

#include "define.h"

#define LEN (100*1024*1024)
char test_buff[LEN];

void readcb(struct mux_socket *sock, const char *data, size_t len, void *arg) {
	printf("%s\n", data);
	printf("len %d\n", len);
	mux_socket_write(sock, "hello", strlen("hello")+1);
	mux_socket_close(sock);
}

void readcb2(struct mux_socket *sock, const char *data, size_t len, void *arg) {
	int *i = (int*)data;
	printf("recv %d\n", *i);
	printf("len %d\n", len);
	mux_socket_close(sock);
}

void eventcb(struct mux_socket *sock, int event, void *arg) {
	printf("user event(%d) call\n", event);
}

void acceptcb(struct mux_socket *sock, void *arg) {
	printf("user acceptcb call\n");
	mux_socket_set_callback(sock, readcb, NULL, eventcb, NULL);
}

void acceptcb2(struct mux_socket *sock, void *arg) {
	printf("user acceptcb2 call\n");
	mux_socket_set_callback(sock, readcb2, NULL, eventcb, NULL);
}

int main() {
	struct event_base* base = event_base_new();
	if(NULL == base) {
		fprintf(stderr, "event_base_new fail\n");
		return -1;
	}

	int i = 0;
	for(; i < sizeof(test_buff); i++) {
		if (i == 0 || (i%(1400-12)) != 0) {
			test_buff[i] = (i%9)+'a';
		} else {
			test_buff[i] = '\0';
		}
	}

	struct mux_listener *server = mux_server_init(base, "0.0.0.0", 8080);
	assert(server);
	
	mux_add_acceptcb(server, "test", acceptcb, NULL);
	mux_add_acceptcb(server, "test2", acceptcb2, NULL);
	
	event_base_dispatch(base);

	mux_listener_free(server);
}
