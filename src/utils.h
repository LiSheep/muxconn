#ifndef __MUX_UTILS_H
#define __MUX_UTILS_H

#include <stdint.h>
#include <stdlib.h>

#include "mux/mux.h"
#include "mux_internal.h"

#define SERVICE_NAME_MAX_LEN 20
#define NIPQUAD_FMT "%d.%d.%d.%d"
#define NIPQUAD_H(addr) \
		((unsigned char*)&(addr))[3], \
        ((unsigned char*)&(addr))[2], \
        ((unsigned char*)&(addr))[1], \
        ((unsigned char*)&(addr))[0] 

char *alloc_proto_msg(struct mux_socket *sock, uint8_t type, uint8_t flag, 
						const char *payload, size_t payload_len, size_t *tot_len);
char *alloc_handshake_msg(struct mux_socket *sock, size_t *tot_len);
char *alloc_connect_handshake_msg(struct mux_socket *sock, const char *service_name, size_t *tot_len);
char *alloc_data_msg(struct mux_socket *sock, const char *payload, size_t len, size_t *tot_len);
char *alloc_pingpong_msg(struct mux *m, size_t *tot_len, int ping);
char *alloc_rst_msg(uint32_t seq, size_t *tot_len);
char *alloc_close_msg(struct mux_socket *sock, size_t *tot_len);
#endif
