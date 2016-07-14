#include "mux/mux.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <event2/event.h>
#include "mux_internal.h"
#include "hashtable.h"
#include "utils.h"

static void __call_ev(void *k, void *v, void *data) {
	struct mux_socket *sock = (struct mux_socket *)v;
	if (sock) {
		sock->status = SOCK_ERROR;
		if (sock->eventcb)
			sock->eventcb(sock, sock->mux->error_ev, sock->arg);
	}
}

void free_seq_map(struct hashtable *map) {
	if (NULL == map)
		return;
	hashtable_travel(map, __call_ev, NULL);
	hashtable_destroy(map, 0, 0);
}

void mux_free(struct mux *m) {
	if(m) {
		if (m->reconnect_timer)
			event_free(m->reconnect_timer);
		if (m->heartbeat_timer)
			event_free(m->heartbeat_timer);
		if (m->bev)
			bufferevent_free(m->bev);
		if (m->seq_map)
			free_seq_map(m->seq_map);

		free(m);
	}
}

int mux_add_acceptcb(struct mux_listener *m, const char *service, mux_accept_cb acceptcb, void *arg) {
	void *key = strdup(service);
	if (NULL == key)
		return -1;
	if (hashtable_insert(m->acceptcbs, key, acceptcb))
		return -1;
	if (hashtable_insert(m->args, key, arg)) {
		hashtable_remove(m->acceptcbs, key);
		return -1;
	}
	return 0;
}

void mux_listener_free(struct mux_listener *m) {
	if (NULL == m)
		return;
	if (m->evlistener)
		evconnlistener_free(m->evlistener);
	if (m->acceptcbs)
		hashtable_destroy(m->acceptcbs, 0, 0);
	if (m->args)
		hashtable_destroy(m->args, 1, 0);
	free(m);
}

void mux_client_set_eventcb(struct mux *mux, mux_client_event_cb cb, void *arg) {
	mux->client_eventcb = cb;
	mux->arg = arg;
}

void mux_set_write_watermask(struct mux *m, size_t mask) {
	assert(mask >= 0);
	assert(m->bev);
	m->write_watermask = mask;
	bufferevent_setwatermark(m->bev, EV_WRITE, mask/2, mask);
}

void mux_listener_set_write_watermask(struct mux_listener *m, size_t mask) {
	assert(mask >= 0);
	m->write_watermask = mask;
}

int mux_socket_status(struct mux_socket *sock) {
	return sock->status;
}

int mux_dealinit_msg(struct mux *mux, char *payload, size_t len) {
	int index = 0;
	int p = 0;
	uint16_t ilen = 0;
	while((len - p) > 0) {
		index = payload[p];
		p++;
		if (index == INI_VERSION) {
			ilen = decode_16u(payload + p);
			if (ilen != sizeof(uint32_t))
				return -1;
			p+=2;
			mux->peer_version = decode_32u(payload + p);
			p+=sizeof(uint32_t);
			if (mux->peer_version < protocol_version())
				mux->used_version = mux->peer_version;
			else
				mux->used_version = protocol_version();
		}
	}
}
