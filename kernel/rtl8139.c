#include "rtl8139.h"
#include "string.h"
#include "timer.h"

u16 rtl8139_io;
u8  rtl8139_mac[6];

static u8  rx_ring[RTL_RX_CAP + 4096] __attribute__((aligned(4096)));
static u32 rx_offset;
static u8  tx_buf[1600] __attribute__((aligned(4)));

static u8 tx_slot;

int rtl8139_init(u16 io_base, u8 *mac_out6) {
    int i;
    u32 rb;

    rtl8139_io = io_base;
    if (!io_base)
        return -1;

    
    outb(io_base + 0x52, 0x00);

    
    outb(io_base + 0x37, 0x10);
    for (i = 0; i < 1000; i++) {
        if (!(inb(io_base + 0x37) & 0x10))
            break;
        sleep_ms(1);
    }

    
    for (i = 0; i < 6; i++) {
        rtl8139_mac[i] = inb(io_base + (u16)i);
        if (mac_out6)
            mac_out6[i] = rtl8139_mac[i];
    }

    
    kmemset(rx_ring, 0, sizeof(rx_ring));
    rx_offset = 0;
    tx_slot   = 0;   

    rb = (u32)(unsigned long)rx_ring;
    outl(io_base + 0x30, rb);
    outl(io_base + 0x44, 0x000f1F8Eu);
    outw(io_base + 0x3C, 0x0005);
    outw(io_base + 0x3E, 0xffff);

    
    outw(io_base + 0x38, (u16)((0u - 0x10u) & 0xFFFCu));  

    
    for (i = 0; i < 4; i++)
        outl(io_base + 0x10u + (u32)i * 4u, 0x00002000u); 

    outb(io_base + 0x37, 0x0C);  
    return 0;
}

void rtl8139_send(u16 io, const void *pkt, int len) {
    u32 paddr = (u32)(unsigned long)tx_buf;
    u32 t0;
    u32 tsd_val;
    u32 tsad_reg = 0x20u + (u32)tx_slot * 4u;
    u32 tsd_reg = 0x10u + (u32)tx_slot * 4u;
    int orig = len;

    if (orig > 1514)
        orig = 1514;
    if (orig < 0)
        return;

    kmemset(tx_buf, 0, sizeof(tx_buf));
    kmemcpy(tx_buf, pkt, (size_t)orig);
    len = orig < 60 ? 60 : orig;

    outl(io + tsad_reg, paddr);
    tsd_val = ((u32)len & 0x1FFFu) | (0x00010000u);
    outl(io + tsd_reg, tsd_val);

    t0 = timer_ticks();
    for (;;) {
        u32 s = inl(io + tsd_reg);
        if (s & 0x8000u) {
            tx_slot = (u8)((tx_slot + 1u) & 3u);
            break;
        }
        timer_poll();
        if ((int)(timer_ticks() - t0) >= 500)
            break;
        __asm__ volatile ("pause");
    }
    outw(io + 0x3E, 0x04);
}

int rtl8139_recv(u16 io, void *buf, int maxlen) {
    volatile u8 *ring = rx_ring;
    u16 pkt_len, raw_len;
    int copy_len;
    u32 off;
    u32 ring_mask = (64u * 1024u) - 1u;
    u16 status;

    
    {
        u16 cbr  = inw(io + 0x3A);
        u16 capr = (u16)((rx_offset - 0x10u) & 0xFFFCu);
        if (cbr == capr)
            return 0;
    }

    off = rx_offset & ring_mask;

    
    status  = (u16)((u16)ring[off] | ((u16)ring[(off + 1u) & ring_mask] << 8));
    raw_len = (u16)((u16)ring[(off + 2u) & ring_mask] |
                    ((u16)ring[(off + 3u) & ring_mask] << 8));

    if (!(status & 0x0001u))   
        return 0;

    if (raw_len < 18u || raw_len > 1518u) {
        
        rx_offset = (rx_offset + 4u) & ring_mask;
        outw(io + 0x38, (u16)((rx_offset - 0x10u) & 0xFFFCu));
        return -1;
    }

    pkt_len  = raw_len - 4u;   
    copy_len = (int)pkt_len;
    if (copy_len > maxlen)
        copy_len = maxlen;

    {
        u32 j;
        u8 *d = (u8 *)buf;
        u32 src = (off + 4u) & ring_mask;
        for (j = 0; j < (u32)copy_len; j++) {
            d[j] = ring[src];
            src  = (src + 1u) & ring_mask;
        }
    }

    {
        u32 adv = ((u32)raw_len + 4u + 3u) & ~3u;
        rx_offset = (rx_offset + adv) & ring_mask;
        outw(io + 0x38, (u16)((rx_offset - 0x10u) & 0xFFFCu));
    }

    return copy_len;
}
