#include "mux/mux.h"
#include "mux/socket.h"

#include <event2/event.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "mux_internal.h"

char snd_buff[1024*1024]; //1m
struct event *snd_ev;
int send_mb; // bytes
static void snd_timercb(int fd, short events, void *ctx) {
	struct mux_socket *sock = ctx;
	assert(sock);
	int times = send_mb;
	for (; times > 0; times--) {
		mux_socket_write(sock, snd_buff, sizeof(snd_buff));
	}

}
void readcb(struct mux_socket *sock, const char *data, size_t len, void *arg) {
	printf("recv %d\n", len);	
}

void eventcb(struct mux_socket *sock, int event, void *arg) {
	printf("user event(%d) call\n", event);
	if (event == MUX_EV_CONNECTED) {
		snd_ev = event_new(sock->mux->base, -1, EV_PERSIST, snd_timercb, sock);
		assert(snd_ev);
		struct timeval timeout;
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;
		event_add(snd_ev, &timeout);
	} else {
		printf("close\n");
		mux_socket_close(sock);
	}
}

void client_eventcb(struct mux *mux, int event, void *arg) {
	if (event == MUX_EV_CONNECTED) {
		struct mux_socket *sock = mux_socket_new(mux);
		assert(sock);
		mux_socket_connect(sock, "test");
		mux_socket_set_callback(sock, readcb, NULL, eventcb, NULL);
	} else {
		if (snd_ev) {
			event_free(snd_ev);
			snd_ev = NULL;
		}
	}
}

// argv 1: server_ip 2:port 3:send_mb(M/s)
int main(int argc, char **argv) {
	struct event_base* base = event_base_new();
	assert(base);
	if (argc != 4) {
		printf("bad argument\n");
		return -1;
	}
	int i = 0;
	for (; i < sizeof(snd_buff); i++)
		snd_buff[i] = 'a'+i%26;
	snd_buff[i-1] = '\0';
	send_mb = atoi(argv[3]);
	printf("send in %d M/s\n", send_mb);
	struct mux *client = mux_client_init(base, argv[1], atoi(argv[2]));
	assert(client);
	mux_client_set_eventcb(client, client_eventcb, NULL);
	mux_set_write_watermask(client, 10241);
	event_base_dispatch(base);
	mux_free(client);
	return 0;
}

