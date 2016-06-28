#include "mux/mux.h"
#include "mux/socket.h"

#include <event2/event.h>
#include <string.h>
#include <assert.h>

char snd_buff[40960]; //4k

void readcb(struct mux_socket *sock, const char *data, size_t len, void *arg) {
	printf("recv %d\n", len); 
	mux_socket_write(sock, snd_buff, sizeof(snd_buff));
//	printf("%s\n", data);
}

void eventcb(struct mux_socket *sock, int event, void *arg) {
	printf("user event(%d) call\n", event);
	mux_socket_close(sock);
}

void acceptcb(struct mux_socket *sock, void *arg) {
	printf("user acceptcb call\n");
	mux_socket_set_callback(sock, readcb, NULL, eventcb, NULL);
}

// argv: 1:port 
int main(int argc, char **argv) {
	struct event_base* base = event_base_new();
	assert(base);

	if (argc != 2) {
		printf("bad argument\n");
		return -1;
	}
	int i = 0;
	for(; i < sizeof(snd_buff); i++) {
		snd_buff[i] = 'a';
	}
	struct mux_listener *server = mux_server_init(base, "0.0.0.0", atoi(argv[1]));
	assert(server);
	mux_add_acceptcb(server, "test", acceptcb, NULL);
	mux_listener_set_write_watermask(server, 10240); 
	event_base_dispatch(base);

	mux_listener_free(server);
}
