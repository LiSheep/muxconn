
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>

#include "mux/mux.h"
#include "mux/socket.h"
#include "mux_internal.h"
#include "utils.h"

int main() {
	struct mux *m = calloc(1, sizeof(struct mux));
	m->sequence = 0;
	struct mux_socket *sock = mux_socket_new(m);
	size_t tot_len = 0;
	assert(sock);
	char *buff = alloc_handshake_msg(sock, &tot_len);
	assert(buff);
	free(buff);
	char *data = "hello";
	int len = strlen(data);
	char *buff2 = alloc_data_msg(sock, data, len, &tot_len);
	free(buff2);
	mux_socket_decref_free(sock);
}
