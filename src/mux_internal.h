#ifndef __MUX_STRUCT_H
#define __MUX_STRUCT_H

#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>

#include <event2/buffer.h>

#include "mux/mux.h"
#include "mux/socket.h"
#include "hashtable.h"

#define MUX_PROTO_SECRET "_MU!X"
#define MUX_PROTO_SECRET_LEN 5
#define MUX_PROTO_HEAD_LEN sizeof(mux_proto_t)

struct mux_listener;

struct mux {
	int type;
	int status;
	uint32_t remote_ip;
	uint16_t remote_port;
	uint32_t local_ip;
	uint16_t local_port;
	size_t write_watermask;
	
	// sock
	uint32_t sequence;
	struct hashtable *seq_map; // seq:sock
	int error_ev;
	
	struct event_base *base;
	struct mux_listener *listener;
	mux_client_event_cb client_eventcb;
	void *arg;
	struct event* reconnect_timer;
	struct event* heartbeat_timer;
	struct bufferevent* bev;
	struct evbuffer* output; // for big write
};

struct mux_listener {
	uint32_t local_ip;
	uint16_t local_port;
	size_t write_watermask;
	struct event_base *base;
	struct evconnlistener *evlistener;
	struct hashtable *acceptcbs; //key:service_name, value:mux_accept_cb acceptcb;
	struct hashtable *args;
};

struct mux_socket {
	uint32_t seq;
	struct mux *mux;
	int status;
	int refcnt;

	mux_data_cb readcb;
	mux_data_cb writecb;
	mux_event_cb eventcb;

	struct evbuffer* recv_buff;
	void *arg;
};

#pragma pack(1)
typedef struct mux_proto_s {
	uint8_t hr:1;
	uint32_t length:31;
	uint8_t type;
	uint8_t flag;
	uint16_t reserve;
	uint32_t sequence;
	char payload[0];
} mux_proto_t;
#pragma pack()

enum mux_proto_type_e {
	PTYPE_DATA = 0,
	PTYPE_HANDSHAKE,
	PTYPE_PING,
	PTYPE_PONG,
	PTYPE_RST,
	PTYPE_CLOSE
};

enum mux_proto_flag_e {
	PFLAG_NONE = 0,
	PFLAG_MORE = 1
};

struct mux_socket *mux_socket_new4server(struct mux *m, uint32_t seq);
struct mux_socket *mux_socket_get(struct mux *m, uint32_t seq);
void mux_socket_incref(struct mux_socket *sock);
void mux_socket_decref_free(struct mux_socket *sock);
void free_seq_map(struct hashtable *map);
int mux_socket_recvdata(struct mux_socket *sock, mux_proto_t *proto);
void sock_cache_writecb(struct bufferevent *bev, void *ctx);
int send_or_cache(struct mux *mux, const char *data, size_t len);

#endif
