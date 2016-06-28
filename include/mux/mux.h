#ifndef __MUXCONN_H
#define __MUXCONN_H

#include <event2/event.h>

struct mux;
struct mux_socket;
struct mux_server;

typedef void (*mux_accept_cb)(struct mux_socket *client_sock, void *arg);
typedef void (*mux_client_event_cb)(struct mux* mux, int event, void *arg);

enum mux_status {
	MUX_CONNECTING = 0,
	MUX_CONNECTED,
	MUX_ESTABLISH,
	MUX_CLOSE
};

enum mux_type {
	MUX_CLIENT = 0,
	MUX_SERVER
};

enum mux_event {
	MUX_EV_CONNECTED = 1,
	MUX_EV_EOF,
	MUX_EV_ERROR,
	MUX_EV_RST // reset by other peer
};

struct mux *mux_client_init(struct event_base* base, const char *remote_ip, int remote_port);

// cb will be called after connected/disconnected to server.
// if not conntected to server, the message your send will be failed
void mux_client_set_eventcb(struct mux *mux, mux_client_event_cb cb, void *arg);

struct mux_listener *mux_server_init(struct event_base* base, const char *local_ip, int local_port);

void mux_free(struct mux *m);

void mux_listener_free(struct mux_listener *m);

int mux_add_acceptcb(struct mux_listener *m, const char *service, mux_accept_cb acceptcb, void *arg);

void mux_set_write_watermask(struct mux *m, size_t mask);

// mask have to >= 10240
void mux_listener_set_write_watermask(struct mux_listener *m, size_t mask);

#endif
