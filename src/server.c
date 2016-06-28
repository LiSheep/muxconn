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

static void __acceptcb(struct evconnlistener *listener, int fd, 
				struct sockaddr *address, int socklen, void *ctx);
static void __accept_errorcb(struct evconnlistener *listener, void *ctx);
static void __server_readcb(struct bufferevent *bev, void *ctx);
static void __server_eventcb(struct bufferevent *bev, short events, void *ctx);


static unsigned int __service_hash(void *k) {
    //icmp_key_t *sk = k;
    uint32_t key = (uint32_t)(*(uint32_t*)k);
    /* Robert Jenkins' 32 bit integer hash function */
    key = (key + 0x7ed55d16) + (key << 12);
    key = (key ^ 0xc761c23c) ^ (key >> 19);
    key = (key + 0x165667b1) + (key << 5);
    key = (key + 0xd3a2646c) ^ (key << 9);
    key = (key + 0xfd7046c5) + (key << 3);
    key = (key ^ 0xb55a4f09) ^ (key >> 16);

    return key;
}

static int __service_eq(void *p1, void *p2) {
	return (strcmp((const char*)p1, (const char*)p2) == 0);
}

static void __acceptcb(struct evconnlistener *listener, int fd, 
				struct sockaddr *address, int socklen, void *ctx) {
	struct mux_listener *server_listener = (struct mux_listener *)ctx;
	assert(server_listener && server_listener->base);
	struct event_base *base = server_listener->base;
	struct bufferevent *bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);
	if(!bev) {
		PEP_ERROR("muxconn: bufferevent_socket_new error");
		goto fail;
	}
	struct timeval timeout;
	timeout.tv_sec = 4;  // heart beat*2
	timeout.tv_usec = 0;
	if(setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
		PEP_ERROR("muxconn: setsockopt SO_RCVTIMEO failed");
		goto fail1;
	}
	struct mux *server = calloc(1, sizeof(struct mux));
	if (NULL == server) {
		PEP_ERROR("muxconn: calloc fail");
		goto fail1;
	}
	server->output = evbuffer_new();
	if (NULL == server->output) {
		PEP_ERROR("muxconn: evbuffer_new fail");
		goto fail2;
	}
	struct sockaddr_in *addr = (struct sockaddr_in *)address;
	server->base = base;
	server->listener = server_listener;
	server->local_ip = server_listener->local_ip;
	server->local_port = server_listener->local_port;
	server->remote_ip = ntohl(addr->sin_addr.s_addr);
	server->remote_port = ntohs(addr->sin_port);
	server->bev = bev;

	PEP_TRACE("muxconn: accept new client "NIPQUAD_FMT":%d", NIPQUAD_H(server->remote_ip), server->remote_port);
	bufferevent_setcb(bev, __server_readcb, sock_cache_writecb, __server_eventcb, server);
	bufferevent_enable(bev, EV_READ | EV_WRITE);
	if (server_listener->write_watermask > 0) {
		server->write_watermask = server_listener->write_watermask;
		bufferevent_setwatermark(server->bev, EV_WRITE, server->write_watermask/2, server->write_watermask);
	}
	return;
fail2:
	free(server);
fail1:
	bufferevent_free(bev);
fail:
	return;
}

static void __accept_errorcb(struct evconnlistener *listener, void *ctx) {
	PEP_ERROR("muxconn: accept fail %s", strerror(errno));
}

static void __server_readcb(struct bufferevent *bev, void *ctx) {
	struct mux *server = (struct mux *)ctx;
	assert(server);
	struct evbuffer* src = bufferevent_get_input(bev);
	size_t length = evbuffer_get_length(src);
	char *mbuff = NULL;
	size_t mlen = 0;
	uint32_t rst_seq = 0;
	PEP_TRACE("muxconn: recv from "NIPQUAD_FMT":%d", NIPQUAD_H(server->remote_ip), server->remote_port);
	// printf("client recv from "NIPQUAD_FMT":%d. length %d\n", NIPQUAD_H(server->remote_ip), server->remote_port, length);
	while(length >= MUX_PROTO_HEAD_LEN) {
		mux_proto_t *proto = (mux_proto_t *)evbuffer_pullup(src, MUX_PROTO_HEAD_LEN);
		if (NULL == proto)
			break;
		if (proto->hr != 0)
			goto error;
		if(length < proto->length + MUX_PROTO_HEAD_LEN)
			break;
		char buff[proto->length + MUX_PROTO_HEAD_LEN];
		bufferevent_read(bev, buff, sizeof(buff));
		proto = (mux_proto_t*)buff;
		struct mux_socket *sock = NULL;
		uint32_t seq = proto->sequence;
		switch (proto->type) {
			case PTYPE_DATA:
				PEP_TRACE("muxconn: recv seq %u, data_len: %d", seq, proto->length);
				sock = mux_socket_get(server, proto->sequence);
				if (NULL == sock) {
					rst_seq = proto->sequence;
					goto rst;
				}
				if (mux_socket_recvdata(sock, proto)) {
					rst_seq = proto->sequence;
					goto rst;
				}
			break;
			case PTYPE_HANDSHAKE:
				PEP_TRACE("muxconn: recv handshake seq %u", seq);
				if (strncmp(proto->payload, MUX_PROTO_SECRET, MUX_PROTO_SECRET_LEN)) {
					rst_seq = proto->sequence;
					goto rst;
				}
				int left_len = proto->length - MUX_PROTO_SECRET_LEN;
				if (left_len > SERVICE_NAME_MAX_LEN) {
					rst_seq = proto->sequence;
					PEP_ERROR("muxconn: recv error service name");
					goto rst;
				}
				char *service_name = proto->payload + MUX_PROTO_SECRET_LEN;
				PEP_TRACE("muxconn: accept service %s", service_name);
				sock = mux_socket_new4server(server, seq);
				if (NULL == sock) {
					rst_seq = proto->sequence;
					goto rst;
				}
				mbuff = alloc_handshake_msg(sock, &mlen);
				if (NULL == mbuff) {
					rst_seq = proto->sequence;
					goto rst;
				}
				bufferevent_write(bev, mbuff, mlen);
				free(mbuff);
				mux_accept_cb acceptcb = hashtable_search(server->listener->acceptcbs, service_name);
				if (acceptcb == NULL) {
					rst_seq = proto->sequence;
					goto rst;
				}
				void *arg = hashtable_search(server->listener->args, service_name);
				acceptcb(sock, arg);
			break;
			case PTYPE_PING:
				PEP_TRACE("muxconn: recv ping");
				mbuff = alloc_pingpong_msg(server, &mlen, 0);
				if (NULL == mbuff) {
					break;
				}
				bufferevent_write(bev, mbuff, mlen);
				free(mbuff);
			break;
			case PTYPE_RST:
				PEP_TRACE("muxconn: recv rst");
				sock = mux_socket_get(server, proto->sequence);
				if (NULL != sock) {
					mux_socket_incref(sock);
					if (sock->eventcb)
						sock->eventcb(sock, MUX_EV_RST, sock->arg);
					mux_socket_decref_free(sock);
				}
			break;
			case PTYPE_CLOSE:
				PEP_TRACE("muxconn: recv close");
				sock = mux_socket_get(server, proto->sequence);
				if (NULL != sock) {
					mux_socket_incref(sock);
					if (sock->eventcb)
						sock->eventcb(sock, MUX_EV_EOF, sock->arg);
					mux_socket_decref_free(sock);
				}
			break;
			default:
			goto error;
		}
		length = evbuffer_get_length(src);
		continue;
		rst:
			mbuff = alloc_rst_msg(rst_seq, &mlen);
			bufferevent_write(bev, mbuff, mlen);
			free(mbuff);
			if(NULL != sock)
				mux_socket_decref_free(sock);
	}
	return;
error:
	PEP_ERROR("muxconn: message error.");
	return;
}

static void __server_eventcb(struct bufferevent *bev, short events, void *ctx) {
	struct mux *server = (struct mux *)ctx;
	if (events & BEV_EVENT_EOF) {
		server->error_ev = MUX_EV_EOF;
		PEP_INFO("muxconn: client close");
	} else {
		server->error_ev = MUX_EV_ERROR;
		PEP_INFO("muxconn: client close %s", strerror(errno));
	}
	free_seq_map(server->seq_map);
	server->seq_map = NULL;
	mux_free(server);
}

struct mux_listener *mux_server_init(struct event_base* base, const char *local_ip, int local_port) {
	struct mux_listener *server_listener = calloc(1, sizeof(struct mux_listener));
	if(NULL == server_listener) {
		goto fail;
	}
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr(local_ip);
	addr.sin_port = htons(local_port);
	server_listener->base = base;
	server_listener->local_ip = ntohl(inet_addr(local_ip));
	server_listener->local_port = local_port;
	server_listener->evlistener = evconnlistener_new_bind(base, __acceptcb, server_listener,
								LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE, -1,
								(struct sockaddr *)&addr, sizeof(addr));
	if (NULL == server_listener->evlistener) {
		PEP_ERROR("muxconn: bind fail %s", strerror(errno));
		goto fail1;
	}
	server_listener->acceptcbs = create_hashtable(10, __service_hash, __service_eq);
	if (NULL == server_listener->acceptcbs) {
		PEP_ERROR("muxconn: create_hashtable fail");
		goto fail2;
	}
	server_listener->args = create_hashtable(10, __service_hash, __service_eq);
	if (NULL == server_listener->args) {
		PEP_ERROR("muxconn: create_hashtable fail");
		goto fail3;
	}
	evconnlistener_set_error_cb(server_listener->evlistener, __accept_errorcb);

	return server_listener;
fail3:
	hashtable_destroy(server_listener->acceptcbs, 0, 0);
fail2:
	evconnlistener_free(server_listener->evlistener);
fail1:
	free(server_listener);
fail:
	return NULL;
}

