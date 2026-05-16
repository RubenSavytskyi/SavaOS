#pragma once
#include "types.h"

int  net_init(void);

int  net_ready(void);

int  net_dns_lookup(const char *hostname, u8 out_ip[4]);

int  net_http_get(const char *host, const char *path, char *out, int out_cap);
