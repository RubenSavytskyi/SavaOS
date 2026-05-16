#pragma once
#include "types.h"

#define RTL_RX_CAP  (64 * 1024 + 16 + 1536)

int  rtl8139_init(u16 io_base, u8 *mac_out6);
void rtl8139_send(u16 io, const void *pkt, int len);
int  rtl8139_recv(u16 io, void *buf, int maxlen);

extern u16 rtl8139_io;
extern u8  rtl8139_mac[6];
