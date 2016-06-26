#include "mux/mux.h"
#include "mux/socket.h"

#include <event2/event.h>
#include <stdio.h>
#include <assert.h>
#include "mux_internal.h"
#include "define.h"

static char buff[20000];
void readcb(struct mux_socket *sock, const char *data, size_t len, void *arg) {
	printf("recv %d\n", len);
}

void eventcb(struct mux_socket *sock, int event, void *arg) {
	printf("user event(%d) call\n", event);
	if (event == MUX_EV_CONNECTED) {
		mux_socket_write(sock, buff, sizeof(buff));
	} else {
		printf("close\n");
		mux_socket_close(sock);
	}
}

void readcb2(struct mux_socket *sock, const char *data, size_t len, void *arg) {
	printf("recv2 %d\n", len);
}

void eventcb2(struct mux_socket *sock, int event, void *arg) {
	printf("user2 event(%d) call\n", event);
	if (event == MUX_EV_CONNECTED) {
		int i = 1234;
		mux_socket_write(sock, (const char*)&i, sizeof(i));
	} else {
		printf("close2\n");
		mux_socket_close(sock);
	}
}

void client_eventcb(struct mux *mux, int event, void *arg) {
	if (event == MUX_EV_CONNECTED) {
		struct mux_socket *sock = mux_socket_new(mux);
		assert(sock);
		mux_socket_connect(sock, "test");
		mux_socket_set_callback(sock, readcb, NULL, eventcb, NULL);

		struct mux_socket *sock2 = mux_socket_new(mux);
		assert(sock2);
		mux_socket_connect(sock2, "test2");
		mux_socket_set_callback(sock2, readcb2, NULL, eventcb2, NULL);
	}
}

int main(int argc, char **argv) {
	struct event_base* base = event_base_new();
	if(NULL == base) {
		fprintf(stderr, "event_base_new fail\n");
		return -1;
	}
	int i = 0;
		for(; i < sizeof(buff) - 1; i++)
			buff[i] = 'a'+ i%26;
	buff[i] = '\0';
	struct mux *client = mux_client_init(base, argv[1], 8080);
	assert(client);
	mux_client_set_eventcb(client, client_eventcb, NULL);
	mux_set_write_watermask(client, 10240);
	event_base_dispatch(base);
	mux_free(client);
}

