#include "version.h"

#include <stdio.h>

static int s_protocol_version = 0;

int protocol_version() {
	if (s_protocol_version)
		return s_protocol_version;

	int v_first, v_last;

	sscanf (PROTOCOL_VERSION, "%d.%d", &v_first, &v_last);

	s_protocol_version = v_first*100 + v_last;
	return s_protocol_version;
}
