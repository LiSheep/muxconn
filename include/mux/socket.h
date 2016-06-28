#ifndef __MUX_SOCKET_H
#define __MUX_SOCKET_H

#include "mux/mux.h"

struct mux_socket;

enum mux_socket_status_e {
	SOCK_INIT,
	SOCK_ESTABLISH,
	SOCK_CLOSE
};

typedef void (*mux_data_cb)(struct mux_socket *client_sock, const char *data, size_t len, void *arg);
typedef void (*mux_event_cb)(struct mux_socket *client_sock, int event, void *arg);

struct mux_socket *mux_socket_new(struct mux *m);

void mux_socket_close(struct mux_socket *sock);

int mux_socket_connect(struct mux_socket *sock, const char *service_name);

// you can't write before sock connected successful
int mux_socket_write(struct mux_socket *sock, const char *data, size_t len);

// the eventcb will not called if you called mux_socket_close to close the socket
void mux_socket_set_callback(struct mux_socket *sock, mux_data_cb readcb, mux_data_cb writecb, mux_event_cb eventcb, void *arg);

#endif
