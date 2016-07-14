#include "utils.h"

#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include "version.h"
#include "mux_internal.h"


uint8_t *encode_32u(uint8_t *dst, uint32_t value) {
	*dst++ = value >> 24;
	*dst++ = value >> 16;
	*dst++ = value >> 8;
	*dst++ = value;
	return dst;
}

uint32_t decode_32u(const uint8_t *src) {
	return (uint32_t)src[0] << 24 | (uint32_t)src[1] << 16 | (uint32_t)src[2] << 8 | src[3];
}

uint8_t *encode_16u(uint8_t *dst, uint16_t value) {
	*dst++ = value >> 8;
	*dst++ = value;
	return dst;
}

uint16_t decode_16u(const uint8_t *src) {
	return (uint32_t)src[0] << 8 | src[1];
}

// out: tot_len proto len
// return proto buff, need free by yourself
char *alloc_proto_msg(struct mux_socket *sock, uint8_t type, uint8_t flag, 
						const char *payload, size_t payload_len, size_t *tot_len) {
	assert(tot_len);
	*tot_len = sizeof(mux_proto_t) + payload_len;
	char *buff = malloc(*tot_len);
	if (NULL == buff) 
		return NULL;
	mux_proto_t *proto = (mux_proto_t*)buff;
	proto->hr = 0;
	proto->reserve = 0;
	proto->type = type;
	proto->flag = flag;
	proto->length = payload_len;
	if (sock)
		proto->sequence = sock->seq;
	else
		proto->sequence = 0;
	if (payload && payload_len > 0)
		memcpy(proto->payload, payload, payload_len);
	return buff;
}

char *alloc_handshake_msg(struct mux_socket *sock, size_t *tot_len) {
	assert(sock);
	return alloc_proto_msg(sock, PTYPE_HANDSHAKE, 0, MUX_PROTO_SECRET, MUX_PROTO_SECRET_LEN, tot_len);
}

char *alloc_initial_msg(struct mux *m, size_t *tot_len) {
	size_t len = 0;
	char buf[20];
	int p = 0;

	buf[p] = INI_VERSION;
	p++;
	encode_16u(buf+p, sizeof(uint32_t));
	p+=2;
	encode_32u(buf+p, protocol_version());
	p+=sizeof(uint32_t);

	len = p;

	return alloc_proto_msg(NULL, PTYPE_INIT, 0, buf, len, tot_len);
}

char *alloc_connect_handshake_msg(struct mux_socket *sock, const char *service_name, size_t *tot_len) {
	assert(sock);
	size_t s_len = strlen(service_name) + 1;
	assert(s_len < SERVICE_NAME_MAX_LEN);
	char payload[MUX_PROTO_SECRET_LEN + s_len];
	memcpy(payload, MUX_PROTO_SECRET, MUX_PROTO_SECRET_LEN);
	memcpy(payload + MUX_PROTO_SECRET_LEN, service_name, s_len);
	return alloc_proto_msg(sock, PTYPE_HANDSHAKE, 0, payload, sizeof(payload), tot_len);
}

char *alloc_data_msg(struct mux_socket *sock, const char *payload, size_t len, size_t *tot_len) {
	assert(len < 0x7FFFFFFF);
	assert(sock);
	return alloc_proto_msg(sock, PTYPE_DATA, 0, payload, len, tot_len);
}

char *alloc_pingpong_msg(struct mux *m, size_t *tot_len, int ping) {
	assert(tot_len);
	char *buff = NULL;
	if (ping)
		buff = alloc_proto_msg(NULL, PTYPE_PING, 0, NULL, 0, tot_len);
	else
		buff = alloc_proto_msg(NULL, PTYPE_PONG, 0, NULL, 0, tot_len);
	return buff;
}


char *alloc_rst_msg(uint32_t seq, size_t *tot_len) {
	return alloc_proto_msg(NULL, PTYPE_RST, 0, NULL, 0, tot_len);
}

char *alloc_close_msg(struct mux_socket *sock, size_t *tot_len) {
	assert(sock);
	return alloc_proto_msg(sock, PTYPE_CLOSE, 0, NULL, 0, tot_len);
}
