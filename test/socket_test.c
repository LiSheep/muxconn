
#include <stdio.h>
#include <assert.h>

#include "mux/mux.h"
#include "mux/socket.h"
#include "mux_internal.h"
#include "utils.h"

int main() {
	struct mux *m = calloc(1, sizeof(struct mux));
	assert(m);
	struct mux_socket *sock = mux_socket_new(m);
	assert(sock);
	assert(sock->seq == 1);
	mux_socket_decref_free(sock);
	
	m->sequence = 4294967295;
	sock = mux_socket_new(m);
	assert(sock->seq == 4294967295);
	mux_socket_decref_free(sock);

	sock = mux_socket_new(m);
	assert(sock->seq == 1);
	mux_socket_decref_free(sock);
}
