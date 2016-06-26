#include "mux/socket.h"

#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include <event2/buffer.h>
#include <event2/bufferevent.h>

#include <mux/mux.h>
#include "hashtable.h"
#include "mux_internal.h"
#include "utils.h"
#include "define.h"

//  socket(sys_call) -> sock_map_fd() -> sock_alloc_fd() -> get_unused_fd_flags() 

// static struct hashtable __fdmap;

// sock_map_fd();
// static int __get_unused_fd(struct mux *m);
// static struct mux_socket *__alloc_fd(struct mux *m);
// static void __free_fd(struct mux *m, int fd);

#define HASHTABLE_SIZE 10000 

static unsigned int __hashf(void *k) {
	uint32_t key = *(uint32_t*)k;
	key = key%(HASHTABLE_SIZE/2);
	return key;
}

static int __eqf(void *k1, void *k2) {
	uint32_t *_k1 = k1;
	uint32_t *_k2 = k2;
	return (*_k1 == *_k2);
}

// static int __get_unused_fd(struct mux *m) {
// 	int i = m->fd_hint;
// 	assert(i > 0);
// 	for(; i <= 65535; i++) {
// 		if(!FD_ISSET(i, &m->mux_fd_set)) 
// 			return i;
// 	}
// 	m->fd_hint = 1;
// 	for(i = 0; i < m->fd_hint; i++) {
// 		if(!FD_ISSET(i, &m->mux_fd_set)) 
// 			return i;
// 	}
// 	return -1;
// }

// static struct mux_socket *__alloc_fd(struct mux *m) {
// 	int fd = __get_unused_fd(m);
// 	if (fd < 0)
// 		goto fail;
	
// 	if (NULL == m->fd_map) {
// 		m->fd_map = create_hashtable(100, __hashf, __eqf);
// 		if(NULL == m->fd_map)
// 			goto fail;
// 	}
	
// 	struct mux_socket *sock = calloc(1, sizeof(struct mux_socket));
// 	if (NULL == sock)
// 		goto fail;
// 	sock->fd = fd;
	
// 	if (hashtable_insert(m->fd_map, &sock->fd, sock) > 0)
// 		goto fail1;

// 	FD_SET(fd, &m->mux_fd_set);
// 	if (fd > m->fd_hint) {
// 		m->fd_hint = fd;
// 		m->fd_hint++;
// 	}
	
// 	return sock;
// fail1:
// 	free(sock);
// fail:
// 	return NULL;
// }

// static void __free_fd(struct mux *m, int fd) {
// 	FD_CLR(fd, &m->mux_fd_set);
// 	struct mux_socket *sock = hashtable_remove2(m->fd_map, &fd);
// 	if((fd + 1) == m->fd_hint)
// 		m->fd_hint--;
// 	free(sock);
// }

static int __send_or_cache(struct mux_socket *sock, const char *data, size_t len) {
	assert(sock->mux->bev);

	struct evbuffer *out_buffer = bufferevent_get_output(sock->mux->bev);
	size_t out_len = evbuffer_get_length(out_buffer);
	if (out_len > sock->mux->write_watermask) {
		// cache
		return evbuffer_add(sock->mux->output, data, len);
	} else {
		// send
		if (evbuffer_get_length(sock->mux->output) > 0) {
			bufferevent_write_buffer(sock->mux->bev, sock->mux->output);
			out_len = evbuffer_get_length(out_buffer);
			if (out_len > sock->mux->write_watermask) {
				return evbuffer_add(sock->mux->output, data, len);
			}
		}
		return bufferevent_write(sock->mux->bev, data, len);
	}
	return -1;
}

void sock_cache_writecb(struct bufferevent *bev, void *ctx) {
	struct mux *mux = (struct mux*)ctx;
	if (evbuffer_get_length(mux->output) > 0) {
		bufferevent_write_buffer(bev, mux->output);
	}
}

struct mux_socket *mux_socket_new(struct mux *m) {
	if (NULL == m)
		goto fail;
	if (m->status < MUX_CONNECTED)
		goto fail;
	struct mux_socket *sock = calloc(1, sizeof(struct mux_socket));
	if (NULL == sock)
		goto fail;
	if (NULL == m->seq_map) {
		m->seq_map = create_hashtable(HASHTABLE_SIZE, __hashf, __eqf);
		if(NULL == m->seq_map)
			goto fail1;
	}
	if (m->sequence == 0)
		m->sequence = 1;
	sock->seq = m->sequence++;
	sock->refcnt = 1;
	if (hashtable_insert(m->seq_map, &sock->seq, sock) > 0)
		goto fail1;
	sock->mux = m;

	return sock;
fail1:
	free(sock);
fail:
	return NULL;
}

struct mux_socket *mux_socket_new4server(struct mux *m, uint32_t seq) {
	if (NULL == m)
		goto fail;
	if (seq == 0)
		goto fail;
	struct mux_socket *sock = calloc(1, sizeof(struct mux_socket));
	if (NULL == sock)
		goto fail;
	if (NULL == m->seq_map) {
		m->seq_map = create_hashtable(100, __hashf, __eqf);
		if(NULL == m->seq_map)
			goto fail1;
	}
	sock->seq = seq;
	sock->mux = m;
	sock->refcnt = 1;
	if (hashtable_insert(m->seq_map, &sock->seq, sock) > 0)
		goto fail1;
	return sock;
fail1:
	free(sock);
fail:
	return NULL;
}

void mux_socket_incref(struct mux_socket *sock) {
	assert(sock);
	++sock->refcnt;
}

void mux_socket_decref_free(struct mux_socket *sock) {
	if (NULL == sock)
		return;
	if(0 != --sock->refcnt)
		return;
	assert(sock->mux);
	hashtable_remove2(sock->mux->seq_map, &sock->seq);
	if (sock->recv_buff)
		evbuffer_free(sock->recv_buff);
	free(sock);
}

void mux_socket_close(struct mux_socket *sock) {
	if (NULL == sock || NULL == sock->mux->bev)
		return;
	assert(sock->mux);
	size_t len = 0;
	char * buff = alloc_close_msg(sock, &len);
	bufferevent_write(sock->mux->bev, buff, len);
	free(buff);
	mux_socket_decref_free(sock);
}

int mux_socket_connect(struct mux_socket *sock, const char *service_name) {
	size_t len = 0;
	char *buff = alloc_connect_handshake_msg(sock, service_name, &len);
	if (NULL == buff)
		return -1;
	if (bufferevent_write(sock->mux->bev, buff, len) != 0) {
		free(buff);
		return -1;
	}
	free(buff);
	return 0;
}

struct mux_socket *mux_socket_get(struct mux *m, uint32_t seq) {
	assert(m);
	if (NULL == m->seq_map)
		return NULL;
	return hashtable_search(m->seq_map, &seq);
}

#define MUX_PROTO_MAX_LEN 4200
#define MUX_PACKAGE_MAX_LEN (MUX_PROTO_MAX_LEN - MUX_PROTO_HEAD_LEN)
static int mux_socket_write_bigdata(struct mux_socket *sock, const char *data, size_t len) {
	assert(sock);
	assert(len > MUX_PACKAGE_MAX_LEN);
	char buff[MUX_PROTO_MAX_LEN];
	mux_proto_t *proto = (mux_proto_t *)buff;
	int left_len = len;
	int ret = 0;
	while(left_len > 0) {
		proto->hr = 0;
		proto->reserve = 0;
		proto->type = PTYPE_DATA;
		proto->sequence = sock->seq;
		if (left_len > MUX_PACKAGE_MAX_LEN) {
			proto->length = MUX_PACKAGE_MAX_LEN;
			proto->flag = PFLAG_MORE;
			memcpy(proto->payload, data, MUX_PACKAGE_MAX_LEN);
			if (sock->mux->write_watermask > 0) {
				ret = __send_or_cache(sock, buff, MUX_PROTO_MAX_LEN);
			} else {
				ret = bufferevent_write(sock->mux->bev, buff, MUX_PROTO_MAX_LEN);
			}
			data = data + MUX_PACKAGE_MAX_LEN;
			left_len -= MUX_PACKAGE_MAX_LEN;
		} else {
			proto->flag = 0;
			proto->length = left_len;
			memcpy(proto->payload, data, left_len);
			if (sock->mux->write_watermask > 0) {
				ret = __send_or_cache(sock, buff, left_len + MUX_PROTO_HEAD_LEN);
			} else {
				ret = bufferevent_write(sock->mux->bev, buff, left_len + MUX_PROTO_HEAD_LEN);
			}
			left_len = 0;
		}
	}
	return ret;
}

int mux_socket_write(struct mux_socket *sock, const char *data, size_t len) {
	assert(sock);
	int ret = 0;
	if (len > MUX_PACKAGE_MAX_LEN)
		return mux_socket_write_bigdata(sock, data, len);
	size_t tot_len = 0;
	char * buff = alloc_data_msg(sock, data, len, &tot_len);
	if (buff == NULL)
		return -1;
	if (sock->mux->write_watermask > 0) {
		ret = __send_or_cache(sock, buff, tot_len);
	} else {
		ret = bufferevent_write(sock->mux->bev, buff, tot_len);
	}
	free(buff);
	return ret;
}

void mux_socket_set_callback(struct mux_socket *sock, mux_data_cb readcb,
								 mux_data_cb writecb, mux_event_cb eventcb, void *arg) {
	assert(sock);
	sock->readcb = readcb;
	sock->writecb = writecb;
	sock->eventcb = eventcb;
	sock->arg = arg;
}

int mux_socket_recvdata(struct mux_socket *sock, mux_proto_t *proto) {
	if (proto->length <= 0 || NULL == proto->payload) {
		PEP_ERROR("muxconn: socket_recvdata error");
		return -1;
	}
	PEP_TRACE("recv flag %d, len %d", proto->flag, proto->length);
	if (proto->flag == PFLAG_MORE) {
		if (NULL == sock->recv_buff) {
			sock->recv_buff = evbuffer_new();
			if (NULL == sock->recv_buff) {
				PEP_ERROR("evbuffer_new fail");
				return -1;
			}
		}
		evbuffer_add(sock->recv_buff, proto->payload, proto->length);
		return 0;
	}
	if (sock->recv_buff) {
		evbuffer_add(sock->recv_buff, proto->payload, proto->length);
		char *buff = evbuffer_pullup(sock->recv_buff, -1);
		mux_socket_incref(sock);
		sock->readcb(sock, buff, evbuffer_get_length(sock->recv_buff), sock->arg);
		evbuffer_free(sock->recv_buff);
		sock->recv_buff = NULL;
		mux_socket_decref_free(sock);
	} else {
		mux_socket_incref(sock);
		sock->readcb(sock, proto->payload, proto->length, sock->arg);
		mux_socket_decref_free(sock);
	}
	return 0;
}
