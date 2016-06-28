#include "mux/mux.h"
#include "mux/socket.h"

#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <stdio.h>
#include <assert.h>

#include <event2/event.h>
#include <event2/event_compat.h>
#include <event2/event_struct.h>
#include <event2/bufferevent.h>
#include <event2/bufferevent_compat.h>
#include <event2/listener.h>
#include <event2/util.h>
#include <event2/buffer.h>

#include "define.h"
#include "mux_internal.h"
#include "utils.h"

static void __client_readcb(struct bufferevent *bev, void *ctx);
static void __client_eventcb(struct bufferevent *bev, short events, void *ctx);
static void __reconnect_timercb(int fd, short events, void *ctx);
static void __heartbeat_timercb(int fd, short events, void *ctx);
static int __connect_server(struct mux *client);
static void __reconnect_server(struct mux *client);

static void __client_readcb(struct bufferevent *bev, void *ctx) {
	struct mux *client = (struct mux *)ctx;
	assert(client);
	struct evbuffer* src = bufferevent_get_input(bev);
	size_t length = evbuffer_get_length(src);
	if (client->status != MUX_CONNECTED && client->status != MUX_ESTABLISH) {
		PEP_ERROR("muxconn: client status error");
		return;
	}
	client->status = MUX_ESTABLISH;
	char *mbuff;
	size_t mlen;
	struct mux_socket *cli_sock = NULL;
	uint32_t rst_seq = 0;
	PEP_TRACE("muxconn: recv from "NIPQUAD_FMT":%d", NIPQUAD_H(client->remote_ip), client->remote_port);
	while(length >= MUX_PROTO_HEAD_LEN) {
		mux_proto_t *proto = (mux_proto_t *)evbuffer_pullup(src, MUX_PROTO_HEAD_LEN);
		if (NULL == proto)
			break;
		if(length < proto->length)
			break;
		char buff[proto->length + MUX_PROTO_HEAD_LEN];
		bufferevent_read(bev, buff, sizeof(buff));
		proto = (mux_proto_t*)buff;
		if (proto->hr != 0)
			goto error;
		switch (proto->type) {
			case PTYPE_HANDSHAKE:
				PEP_TRACE("muxconn: recv handshake seq %u", proto->sequence);
				if (strncmp(proto->payload, MUX_PROTO_SECRET, proto->length)) {
					rst_seq = proto->sequence;
					goto rst;
				}
				cli_sock = mux_socket_get(client, proto->sequence);
				if (NULL == cli_sock) {
					rst_seq = proto->sequence;
					goto rst;
				}
				cli_sock->status = SOCK_ESTABLISH;
				if (cli_sock && cli_sock->eventcb)
					cli_sock->eventcb(cli_sock, MUX_EV_CONNECTED, cli_sock->arg);
			break;
			case PTYPE_PONG:
				PEP_TRACE("muxconn: recv pong");
			break;
			case PTYPE_DATA:
				PEP_TRACE("muxconn: recv data");
				cli_sock = mux_socket_get(client, proto->sequence);
				if (NULL == cli_sock) {
					rst_seq = proto->sequence;
					goto rst;
				}
				if (mux_socket_recvdata(cli_sock, proto)) {
					rst_seq = proto->sequence;
					goto rst;
				}
			break;
			case PTYPE_RST:
				PEP_TRACE("muxconn: recv rst");
				cli_sock = mux_socket_get(client, proto->sequence);
				if (NULL != cli_sock) {
					mux_socket_incref(cli_sock);
					if (cli_sock->eventcb)
						cli_sock->eventcb(cli_sock, MUX_EV_RST, cli_sock->arg);
					mux_socket_decref_free(cli_sock);
				}
			break;
			case PTYPE_CLOSE:
				PEP_TRACE("muxconn: recv close");
				cli_sock = mux_socket_get(client, proto->sequence);
				if (NULL != cli_sock) {
					mux_socket_incref(cli_sock);
					if (cli_sock->eventcb)
						cli_sock->eventcb(cli_sock, MUX_EV_EOF, cli_sock->arg);
					mux_socket_decref_free(cli_sock);
				}
			break;
			default:
			PEP_ERROR("muxconn: type %d error", proto->type);
			goto error;
		}
		length = evbuffer_get_length(src);
		continue;
		rst:
			mbuff = alloc_rst_msg(rst_seq, &mlen);
			bufferevent_write(bev, mbuff, mlen);
			free(mbuff);
			if(NULL != cli_sock)
				mux_socket_decref_free(cli_sock);
	}
	return;
error:
	PEP_TRACE("muxconn: drain all");
	evbuffer_drain(src, -1);
	return;
}

static void __client_eventcb(struct bufferevent *bev, short events, void *ctx) {
	struct mux *client = ctx;
	if (events & BEV_EVENT_CONNECTED) {
		event_del(client->reconnect_timer);
		struct timeval timeout;
		timeout.tv_sec = 2;
		timeout.tv_usec = 0;
		event_add(client->heartbeat_timer, &timeout);
		client->bev = bev;
		client->status = MUX_CONNECTED;
		if (client->client_eventcb)
			client->client_eventcb(client, MUX_EV_CONNECTED, client->arg);
	} else if (events & BEV_EVENT_EOF) {
		client->error_ev = MUX_EV_EOF;
		if (client->client_eventcb)
			client->client_eventcb(client, MUX_EV_EOF, client->arg);
		goto reconnect;
	}  else {
		client->error_ev = MUX_EV_ERROR;
		if (client->client_eventcb)
			client->client_eventcb(client, MUX_EV_ERROR, client->arg);
		goto reconnect;
	}
	return;
reconnect:
	free_seq_map(client->seq_map);
	client->seq_map = NULL;
	client->status = MUX_CLOSE;
	__reconnect_server(client);
}

static void __heartbeat_timercb(int fd, short events, void *ctx) {
	struct mux *client = (struct mux *)ctx;
	assert(client && client->bev);
	// printf("heartbeat_timer\n");
	size_t len;
	char *buff = alloc_pingpong_msg(client, &len, 1);
	if (NULL == buff)
		return;
	bufferevent_write(client->bev, buff, len);
	free(buff);
}

static void __reconnect_timercb(int fd, short events, void *ctx) {
	PEP_INFO("muxconn: reconnecting to server....");
	__connect_server((struct mux *)ctx);
}

static int __connect_server(struct mux *client) {
	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(client->remote_ip);
	addr.sin_port = htons(client->remote_port);

	client->bev = bufferevent_socket_new(client->base, -1, BEV_OPT_CLOSE_ON_FREE);
	if (NULL == client->bev) {
		PEP_ERROR("muxconn: bufferevent_socket_new fail");
		return -1;
	}

	if (bufferevent_socket_connect(client->bev, (struct sockaddr*)&addr, sizeof(addr))) {
		PEP_ERROR("muxconn: bufferevent_socket_connect failed: %s", strerror(errno));
		bufferevent_free(client->bev);
		client->bev = NULL;
		return -1;
	}

	bufferevent_setcb(client->bev, __client_readcb, sock_cache_writecb, __client_eventcb, client);
	bufferevent_enable(client->bev, EV_WRITE|EV_READ);
	return 0;
}

static void __reconnect_server(struct mux *client) {
	struct timeval timeout;
	event_del(client->heartbeat_timer);
	if (client->bev) {
		bufferevent_free(client->bev);
		client->bev = NULL;
	}

	timeout.tv_sec = 1; 
	timeout.tv_usec = 0;
	event_add(client->reconnect_timer, &timeout);
}

struct mux *mux_client_init(struct event_base* base, const char *remote_ip, int remote_port) {
	struct mux *client = calloc(1, sizeof(struct mux));
	if (NULL == client) {
		goto fail;
	}
	assert(base);
	client->remote_ip = ntohl(inet_addr(remote_ip));
	client->remote_port = remote_port;
	client->base = base;
	client->status = MUX_CONNECTING;

	client->reconnect_timer = event_new(base, -1, EV_PERSIST, __reconnect_timercb, client);
	if (NULL == client->reconnect_timer) {
		PEP_ERROR("muxconn: event_new fail");
		goto fail1;
	}

	client->heartbeat_timer = event_new(base, -1, EV_PERSIST, __heartbeat_timercb, client);
	if (NULL == client->heartbeat_timer) {
		PEP_ERROR("muxconn: event_new fail");
		goto fail2;
	}
	client->output = evbuffer_new();
	if (NULL == client->output) {
		PEP_ERROR("muxconn: evbuffer_new fail");
		goto fail3;
	}
	if (__connect_server(client) == -1) {
		__reconnect_server(client);
	}
	return client;
fail3:
	event_free(client->heartbeat_timer);
fail2:
	event_free(client->reconnect_timer);
fail1:
	free(client);
fail:
	return NULL;
}

int mux_client_sendto(struct mux *client, const char *paylod, size_t len) {
	if (NULL == client->bev)
		return -1;
	bufferevent_write(client->bev, paylod, len);
	return 0;
}
