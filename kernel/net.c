#include "net.h"
#include "pci.h"
#include "rtl8139.h"
#include "string.h"
#include "timer.h"

static int s_net_ok;
static u8  gw_mac[6];
static int gw_mac_ok;

static u16 ip_id_counter = 1;

#define ETHERTYPE_ARP 0x0806u
#define ETHERTYPE_IP  0x0800u

#define IP_PROTO_UDP 17u
#define IP_PROTO_TCP 6u

static u16 htons(u16 v) { return (u16)((v << 8) | (v >> 8)); }

static u32 ip_sum_acc_be(const u8 *d, int len, u32 s) {
    int i;
    for (i = 0; i + 1 < len; i += 2)
        s += ((u32)d[i] << 8) | (u32)d[i + 1];
    if (i < len)
        s += (u32)d[i] << 8;
    return s;
}

static u16 ip_sum_finish(u32 s) {
    while (s >> 16)
        s = (s & 0xffffu) + (s >> 16);
    return (u16)~s;
}

static int parse_ipv4(const char *s, u8 out4[4]) {
    int p = 0, o = 0, n = 0, digits = 0;
    while (s[p]) {
        if (s[p] >= '0' && s[p] <= '9') {
            n = n * 10 + (s[p] - '0');
            digits++;
            if (n > 255 || digits > 3)
                return -1;
            p++;
        } else if (s[p] == '.') {
            if (o >= 4 || digits == 0)
                return -1;
            out4[o++] = (u8)n;
            n = 0;
            digits = 0;
            p++;
        } else
            return -1;
    }
    if (o != 3 || digits == 0)
        return -1;
    out4[3] = (u8)n;
    return 0;
}

static void net_handle_tcp(const u8 *ip, int ip_len);
static void net_handle_udp(const u8 *ip, int ip_len);
static void tcp_send(u32 seq, u32 ack, u8 flags, const u8 *pl, int plen);

static u8  ip_tcp_rasm[1600];
static int ip_tcp_rasm_len;
static u16 ip_tcp_rasm_id;

static u8  ip_udp_rasm[2048];
static int ip_udp_rasm_len;
static u16 ip_udp_rasm_id;

static void net_handle_ip(const u8 *p, int n) {
    u8 ihl;
    u16 tot;
    u8 proto;
    u8 dst[4];
    u16 fragw;
    u16 foff;
    int mf;
    u16 ip_id;

    if (n < 20)
        return;
    if ((p[0] >> 4) != 4)
        return;
    ihl = (u8)((p[0] & 0x0Fu) * 4u);
    if (ihl < 20 || ihl > 60 || n < (int)ihl)
        return;
    tot = (u16)(((u16)p[2] << 8) | (u16)p[3]);
    if (tot < ihl || (int)tot > n)
        return;

    dst[0] = p[16];
    dst[1] = p[17];
    dst[2] = p[18];
    dst[3] = p[19];
if (dst[0] == 255)
    goto ip_accept;   
if (dst[0] != 10 || dst[1] != 0 || dst[2] != 2 || dst[3] != 15)
    return;
ip_accept:;

    fragw = (u16)(((u16)p[6] << 8) | (u16)p[7]);
    foff = (u16)((fragw & 0x1FFFu) * 8u);
    mf = (int)((fragw >> 13) & 1);
    ip_id = (u16)(((u16)p[4] << 8) | (u16)p[5]);
    proto = p[9];

    
    if (foff != 0 && proto == (u8)IP_PROTO_TCP) {
        u8 i0;
        int pay2;
        int ins;
        if (ip_tcp_rasm_len <= 0 || ip_id != ip_tcp_rasm_id)
            return;
        i0 = (u8)((ip_tcp_rasm[0] & 0x0Fu) * 4u);
        if (i0 < 20 || i0 > 60)
            goto rasm_abort;
        pay2 = (int)tot - (int)ihl;
        if (pay2 < 0 || (int)tot > n)
            goto rasm_abort;
        if ((int)foff != ip_tcp_rasm_len - (int)i0)
            goto rasm_abort;
        ins = (int)i0 + (int)foff;
        if (ins + pay2 > (int)sizeof(ip_tcp_rasm))
            goto rasm_abort;
        kmemcpy(ip_tcp_rasm + ins, p + ihl, (size_t)pay2);
        ip_tcp_rasm_len = ins + pay2;
        if (mf == 0) {
            u16 full;
            if (ip_tcp_rasm_len < (int)i0 + 20)
                goto rasm_abort;
            full = (u16)ip_tcp_rasm_len;
            ip_tcp_rasm[2] = (u8)(full >> 8);
            ip_tcp_rasm[3] = (u8)full;
            ip_tcp_rasm[6] &= 0xE0u;
            ip_tcp_rasm[7] = 0;
            net_handle_tcp(ip_tcp_rasm, (int)full);
            ip_tcp_rasm_len = 0;
        }
        return;
    rasm_abort:
        ip_tcp_rasm_len = 0;
        return;
    }

    
    if (foff != 0 && proto == (u8)IP_PROTO_UDP) {
        u8 i0;
        int pay2;
        int ins;
        if (ip_udp_rasm_len <= 0)
            return;
        if (ip_id != ip_udp_rasm_id) {
            ip_udp_rasm_len = 0;
            return;
        }
        i0 = (u8)((ip_udp_rasm[0] & 0x0Fu) * 4u);
        if (i0 < 20 || i0 > 60)
            goto udp_rasm_abort;
        pay2 = (int)tot - (int)ihl;
        if (pay2 < 0 || (int)tot > n)
            goto udp_rasm_abort;
        if ((int)foff != ip_udp_rasm_len - (int)i0)
            goto udp_rasm_abort;
        ins = (int)i0 + (int)foff;
        if (ins + pay2 > (int)sizeof(ip_udp_rasm))
            goto udp_rasm_abort;
        kmemcpy(ip_udp_rasm + ins, p + ihl, (size_t)pay2);
        ip_udp_rasm_len = ins + pay2;
        if (mf == 0) {
            u16 full;
            if (ip_udp_rasm_len < (int)i0 + 8)
                goto udp_rasm_abort;
            full = (u16)ip_udp_rasm_len;
            ip_udp_rasm[2] = (u8)(full >> 8);
            ip_udp_rasm[3] = (u8)full;
            ip_udp_rasm[6] &= 0xE0u;
            ip_udp_rasm[7] = 0;
            net_handle_udp(ip_udp_rasm, (int)full);
            ip_udp_rasm_len = 0;
        }
        return;
    udp_rasm_abort:
        ip_udp_rasm_len = 0;
        return;
    }

    
    if ((mf != 0 || foff != 0) && proto != (u8)IP_PROTO_TCP && proto != (u8)IP_PROTO_UDP) {
        ip_tcp_rasm_len = 0;
        ip_udp_rasm_len = 0;
        return;
    }

    
    if (mf != 0 && foff == 0 && proto == (u8)IP_PROTO_TCP) {
        if ((int)tot > (int)sizeof(ip_tcp_rasm))
            return;
        ip_udp_rasm_len = 0;
        ip_tcp_rasm_id = ip_id;
        kmemcpy(ip_tcp_rasm, p, (size_t)tot);
        ip_tcp_rasm_len = (int)tot;
        return;
    }

    
    if (mf != 0 && foff == 0 && proto == (u8)IP_PROTO_UDP) {
        if ((int)tot > (int)sizeof(ip_udp_rasm))
            return;
        ip_tcp_rasm_len = 0;
        ip_udp_rasm_id = ip_id;
        kmemcpy(ip_udp_rasm, p, (size_t)tot);
        ip_udp_rasm_len = (int)tot;
        return;
    }

    if (proto == IP_PROTO_UDP)
        net_handle_udp(p, (int)tot);
    else if (proto == IP_PROTO_TCP)
        net_handle_tcp(p, (int)tot);
}

static void net_handle_eth(u8 *f, int n) {
    u16 et;
    if (n < 14)
        return;
    et = (u16)(((u16)f[12] << 8) | (u16)f[13]);
    kprintf("[eth] et=0x%x n=%d\n", (u32)et, n);  
    if (et == (u16)ETHERTYPE_ARP) {
        if (n < 42)
            return;
        if (f[14] == 0x00 && f[15] == 0x01 && f[16] == 0x08 && f[17] == 0x00 && f[18] == 0x06 && f[19] == 0x04) {
            u16 op = (u16)(((u16)f[20] << 8) | (u16)f[21]);
            if (op == 2) {
                if (f[28] == 10 && f[29] == 0 && f[30] == 2 && f[31] == 2) {
                    int i;
                    for (i = 0; i < 6; i++) gw_mac[i] = f[22 + i];
                    gw_mac_ok = 1;
                }
            }
        }
    } else if (et == (u16)ETHERTYPE_IP) {
        net_handle_ip(f + 14, n - 14);
    }
}

static void spin_rx(int rounds) {
    u8 frame[1600];
    int r, n;
    int pkts;
    for (r = 0; r < rounds; r++) {
        timer_poll();
        pkts = 0;
        for (;;) {
            if (pkts++ > 256)
                break;
            n = rtl8139_recv(rtl8139_io, frame, (int)sizeof(frame));
            if (n <= 0)
                break;
            kprintf("[rx] n=%d\n", n);  
            net_handle_eth(frame, n);
        }
    }
}

static void arp_who_has_gw(void) {
    u8 p[64];
    int i;
    kmemset(p, 0, sizeof(p));
    for (i = 0; i < 6; i++) p[i] = 0xFF;
    for (i = 0; i < 6; i++) p[6 + i] = rtl8139_mac[i];
    p[12] = 0x08;
    p[13] = 0x06;
    p[14] = 0x00;
    p[15] = 0x01;
    p[16] = 0x08;
    p[17] = 0x00;
    p[18] = 6;
    p[19] = 4;
    p[20] = 0x00;
    p[21] = 0x01;
    for (i = 0; i < 6; i++) p[22 + i] = rtl8139_mac[i];
    p[28] = 10;
    p[29] = 0;
    p[30] = 2;
    p[31] = 15;
    for (i = 0; i < 6; i++) p[32 + i] = 0;
    p[38] = 10;
    p[39] = 0;
    p[40] = 2;
    p[41] = 2;
    rtl8139_send(rtl8139_io, p, 42);
}

static void eth_send(const u8 *payload, int paylen) {
    u8 pkt[1600];
    int i;
    if (14 + paylen > (int)sizeof(pkt))
        return;
    for (i = 0; i < 6; i++) pkt[i] = gw_mac[i];
    for (i = 0; i < 6; i++) pkt[6 + i] = rtl8139_mac[i];
    pkt[12] = 0x08;
    pkt[13] = 0x00;
    kmemcpy(pkt + 14, payload, (size_t)paylen);
    rtl8139_send(rtl8139_io, pkt, 14 + paylen);
}

static void ip_send(const u8 *ip_payload, int iplen, u8 proto, const u8 dst[4]) {
    u8 iph[20];
    u16 tot = (u16)(20 + iplen);
    u16 csum;
    u8 pkt[1600];
    iph[0] = 0x45;
    iph[1] = 0;
    iph[2] = (u8)(tot >> 8);
    iph[3] = (u8)tot;
    iph[4] = (u8)(ip_id_counter >> 8);
    iph[5] = (u8)ip_id_counter;
    ip_id_counter++;
    
    iph[6] = 0;
    iph[7] = 0;
    iph[8] = 64;
    iph[9] = proto;
    iph[10] = 0;
    iph[11] = 0;
    iph[12] = 10;
    iph[13] = 0;
    iph[14] = 2;
    iph[15] = 15;
    iph[16] = dst[0];
    iph[17] = dst[1];
    iph[18] = dst[2];
    iph[19] = dst[3];
    csum = ip_sum_finish(ip_sum_acc_be(iph, 20, 0));
    iph[10] = (u8)(csum >> 8);
    iph[11] = (u8)csum;
    kmemcpy(pkt, iph, 20);
    kmemcpy(pkt + 20, ip_payload, (size_t)iplen);
    eth_send(pkt, 20 + iplen);
}

static u16 dns_udp_sport;

static int dns_rx_armed;

static u8  dns_pkt[2048];
static int dns_len;
static u16 dns_txid;
static int dns_done;

static int dbg_udp_calls      = 0;
static int dbg_udp_drop_armed = 0;
static int dbg_udp_drop_src   = 0;
static int dbg_udp_drop_port  = 0;
static int dbg_udp_drop_qr    = 0;
static int dbg_udp_drop_tid   = 0;
static int dbg_udp_matched    = 0;

static void net_handle_udp(const u8 *ip, int ip_len) {
    u8 ihl;
    int ulen_hdr;
    int ip_pay;
    int dns_pay;
    const u8 *udp;
    u16 sport, dport;
    u16 tid_pkt;
    const u8 *dns;

    dbg_udp_calls++;

    if (!dns_rx_armed) { dbg_udp_drop_armed++; return; }
    if (dns_done)      { return; }

    ihl = (u8)((ip[0] & 0x0Fu) * 4u);
    udp = ip + ihl;
    if (ip_len < ihl + 8) { dbg_udp_drop_src++; return; }

    if (ip[12] != 10 || ip[13] != 0 || ip[14] != 2 || (ip[15] != 3 && ip[15] != 2))
        { dbg_udp_drop_src++; return; }

    sport = (u16)(((u16)udp[0] << 8) | (u16)udp[1]);
    dport = (u16)(((u16)udp[2] << 8) | (u16)udp[3]);
    (void)sport;

    if (dport != dns_udp_sport) { dbg_udp_drop_port++; return; }

    ulen_hdr = (int)(((u16)udp[4] << 8) | (u16)udp[5]);
    if (ulen_hdr < 8) { dbg_udp_drop_src++; return; }

    ip_pay  = ip_len - (int)ihl - 8;
    if (ip_pay < 12) { dbg_udp_drop_src++; return; }

    dns_pay = ulen_hdr - 8;
    if (dns_pay > ip_pay) dns_pay = ip_pay;
    if (dns_pay < 12 && ip_pay >= 12) dns_pay = ip_pay;
    if (dns_pay < 12) { dbg_udp_drop_src++; return; }

    dns = udp + 8;
    if ((dns[2] & 0x80u) == 0) { dbg_udp_drop_qr++;  return; }

    tid_pkt = (u16)(((u16)dns[0] << 8) | (u16)dns[1]);
    if (tid_pkt != dns_txid)    { dbg_udp_drop_tid++; return; }

    if (dns_pay > (int)sizeof(dns_pkt)) { dbg_udp_drop_src++; return; }

    kmemcpy(dns_pkt, dns, (size_t)dns_pay);
    dns_len  = dns_pay;
    dns_done = 1;
    dbg_udp_matched++;
}

static u16 tcp_sport, tcp_dport;
static u8  tcp_dst[4];
static u32 tcp_iss;
static u32 tcp_snd_nxt;
static u32 tcp_rcv_nxt;
static int tcp_state;
enum { T_IDLE = 0, T_SYN_SENT, T_ESTAB, T_FIN };

#define TCP_ASM_MAX (40 * 1024)
static u8  tcp_asm[TCP_ASM_MAX];
static int tcp_asm_len;

static void net_handle_tcp(const u8 *ip, int ip_len) {
    u8 ihl;
    int tcp_len;
    const u8 *tcp;
    u16 sport, dport;
    u32 seq;
    u8 flg;
    int thlen;
    int paylen;
    const u8 *pay;

    ihl = (u8)((ip[0] & 0x0Fu) * 4u);
    tcp = ip + ihl;
    tcp_len = ip_len - (int)ihl;
    if (tcp_len < 20)
        return;
    sport = (u16)(((u16)tcp[0] << 8) | (u16)tcp[1]);
    dport = (u16)(((u16)tcp[2] << 8) | (u16)tcp[3]);
    if (dport != tcp_sport || sport != tcp_dport)
        return;
    flg = tcp[13];
    
    if ((flg & 0x04u) != 0) {
        tcp_state = T_IDLE;
        return;
    }
    seq = ((u32)tcp[4] << 24) | ((u32)tcp[5] << 16) | ((u32)tcp[6] << 8) | (u32)tcp[7];
    thlen = (int)((tcp[12] >> 4) * 4);
    if (thlen < 20 || thlen > tcp_len)
        return;
    pay = tcp + thlen;
    paylen = tcp_len - thlen;

    if (tcp_state == T_SYN_SENT && (flg & 0x12) == 0x12) {
        kprintf("[tcp] rcv sport=%d dport=%d | exp sport=%d dport=%d state=%d flg=0x%x\n",
        (int)sport, (int)dport, (int)tcp_sport, (int)tcp_dport, tcp_state, (int)flg);
        tcp_rcv_nxt = seq + 1u;
        tcp_state = T_ESTAB;
        tcp_asm_len = 0;
        return;
    }
    if (tcp_state == T_ESTAB) {
        if (seq == tcp_rcv_nxt) {
            if (paylen > 0) {
                int room = TCP_ASM_MAX - tcp_asm_len - 1;
                if (room > paylen)
                    room = paylen;
                if (room > 0) {
                    kmemcpy(tcp_asm + tcp_asm_len, pay, (size_t)room);
                    tcp_asm_len += room;
                }
                tcp_rcv_nxt = seq + (u32)paylen;
            }
            if ((flg & 0x01u) != 0) {
                
                tcp_rcv_nxt += 1u;
                tcp_state = T_FIN;
            }
        }
        
        tcp_send(tcp_snd_nxt, tcp_rcv_nxt, 0x10, 0, 0);
    }
}

static u16 trans_cksum(const u8 *srcip, const u8 *dstip, u8 proto, const u8 *tp, int tlen) {
    u32 s = 0;
    s += (u32)srcip[0] << 8 | (u32)srcip[1];
    s += (u32)srcip[2] << 8 | (u32)srcip[3];
    s += (u32)dstip[0] << 8 | (u32)dstip[1];
    s += (u32)dstip[2] << 8 | (u32)dstip[3];
    s += (u32)proto;
    s += (u32)tlen;
    s = ip_sum_acc_be(tp, tlen, s);
    return ip_sum_finish(s);
}

static void tcp_send(u32 seq, u32 ack, u8 flags, const u8 *pl, int plen) {
    u8 t[2048];
    u8 sip[4] = {10, 0, 2, 15};
    u16 cs;
    int thlen;
    int tl;
    int syn_mss = ((flags & 0x02u) != 0 && plen == 0);

    thlen = syn_mss ? 24 : 20;
    if (thlen + plen > (int)sizeof(t))
        return;
    kmemset(t, 0, (size_t)(thlen + plen));
    t[0] = (u8)(tcp_sport >> 8);
    t[1] = (u8)tcp_sport;
    t[2] = (u8)(tcp_dport >> 8);
    t[3] = (u8)tcp_dport;
    t[4] = (u8)(seq >> 24);
    t[5] = (u8)(seq >> 16);
    t[6] = (u8)(seq >> 8);
    t[7] = (u8)seq;
    t[8] = (u8)(ack >> 24);
    t[9] = (u8)(ack >> 16);
    t[10] = (u8)(ack >> 8);
    t[11] = (u8)ack;
    t[12] = (u8)((thlen / 4) << 4);
    t[13] = flags;
    
    t[14] = 0xff;
    t[15] = 0xff;
    if (syn_mss) {
        
        t[20] = 0x02;
        t[21] = 0x04;
        t[22] = 0x02;
        t[23] = 0x18;
    }
    if (plen > 0)
        kmemcpy(t + thlen, pl, (size_t)plen);
    tl = thlen + plen;
    cs = trans_cksum(sip, tcp_dst, (u8)IP_PROTO_TCP, t, tl);
    if (cs == 0)
        cs = 0xFFFFu;
    t[16] = (u8)(cs >> 8);
    t[17] = (u8)cs;
    ip_send(t, tl, (u8)IP_PROTO_TCP, tcp_dst);
}

static void udp_send(const u8 *udpbody, int ulen, const u8 dst[4], u16 dport) {
    u8 u[576];
    u8 sip[4] = {10, 0, 2, 15};
    u16 cs;
    u16 *w;
    int tot = 8 + ulen;
    if (tot > (int)sizeof(u))
        return;
    kmemset(u, 0, (size_t)tot);
    w = (u16 *)u;
    w[0] = htons(dns_udp_sport);
    w[1] = htons(dport);
    w[2] = htons((u16)tot);
    w[3] = 0;
    kmemcpy(u + 8, udpbody, (size_t)ulen);
    cs = trans_cksum(sip, dst, (u8)IP_PROTO_UDP, u, tot);
    if (cs == 0)
        cs = 0xFFFFu;
    u[6] = (u8)(cs >> 8);
    u[7] = (u8)cs;
    ip_send(u, tot, (u8)IP_PROTO_UDP, dst);
}

int net_init(void) {
    u16 io;
    int t;
    s_net_ok  = 0;
    gw_mac_ok = 0;

    
    dns_done     = 0;
    dns_rx_armed = 0;
    dns_len      = 0;
    tcp_state    = T_IDLE;
    tcp_asm_len  = 0;
    ip_tcp_rasm_len = 0;
    ip_udp_rasm_len = 0;

    io = pci_find_rtl8139_io();
    if (!io)
        return -1;
    if (rtl8139_init(io, 0) != 0)
        return -1;

    
    for (t = 0; t < 10 && !gw_mac_ok; t++) {
        arp_who_has_gw();
        spin_rx(50);   
        sleep_ms(20);  
    }
    if (!gw_mac_ok)
        return -1;
    s_net_ok = 1;
    return 0;
}

int net_ready(void) { return s_net_ok; }

static int dns_skip_name(const u8 *u, int dns_len, int off) {
    int jumped = 0;
    int resume = 0;
    int guard = 0;

    for (;;) {
        if (off < 0 || off >= dns_len)
            return -1;
        if (++guard > 64)
            return -1;
        {
            u8 c = u[off];
            if (c == 0) {
                off++;
                return jumped ? resume : off;
            }
            if ((c & 0xC0u) == 0xC0u) {
                if (off + 2 > dns_len)
                    return -1;
                if (!jumped) {
                    jumped = 1;
                    resume = off + 2;
                }
                off = (int)(((c & 0x3Fu) << 8) | u[off + 1]);
                continue;
            }
            if (c > 63u)
                return -1;
            off += 1 + (int)c;
        }
    }
}

static int dns_try_slirp_a(const u8 *u, int len, u8 out[4]) {
    int p;
    for (p = 12; p + 16 <= len; p++) {
        if (u[p] != 0xC0u || u[p + 1] != 0x0Cu)
            continue;
        if (u[p + 2] != 0 || u[p + 3] != 1u)
            continue;
        if (u[p + 4] != 0 || u[p + 5] != 1u)
            continue;
        if (u[p + 10] != 0 || u[p + 11] != 4u)
            continue;
        out[0] = u[p + 12];
        out[1] = u[p + 13];
        out[2] = u[p + 14];
        out[3] = u[p + 15];
        return 0;
    }
    return -1;
}

int net_dns_lookup(const char *hostname, u8 out_ip[4]) {
    u8 q[300];
    u8 *p;
    int qh, i, lab, attempt;
    u8 dnsd[4] = {10, 0, 2, 3};

    if (!s_net_ok)
        return -1;
    if (parse_ipv4(hostname, out_ip) == 0)
        return 0;
dbg_udp_calls = dbg_udp_drop_armed = dbg_udp_drop_src = 0;
dbg_udp_drop_port = dbg_udp_drop_qr = dbg_udp_drop_tid = dbg_udp_matched = 0;
    for (attempt = 0; attempt < 2; attempt++) {
        dns_done = 0;
        dns_len = 0;
        ip_udp_rasm_len = 0;

        dns_txid = (u16)(timer_ticks() ^ 0xACE1u ^ (u16)(attempt * 0x37u));

        p = q;
        *p++ = (u8)(dns_txid >> 8);
        *p++ = (u8)dns_txid;
        *p++ = 0x01;
        *p++ = 0x00;
        *p++ = 0x00;
        *p++ = 0x01;
        *p++ = 0x00;
        *p++ = 0x00;
        *p++ = 0x00;
        *p++ = 0x00;
        *p++ = 0x00;
        *p++ = 0x00;
        qh = 12;
        {
            const char *h = hostname;
            while (*h) {
                lab = 0;
                while (h[lab] && h[lab] != '.' && lab < 63)
                    lab++;
                if (lab <= 0)
                    return -1;
                q[qh++] = (u8)lab;
                for (i = 0; i < lab; i++) q[qh++] = (u8)h[i];
                h += lab;
                if (*h == '.')
                    h++;
            }
            q[qh++] = 0;
        }
        q[qh++] = 0x00;
        q[qh++] = 0x01;
        q[qh++] = 0x00;
        q[qh++] = 0x01;

        dns_udp_sport = (u16)((timer_ticks() ^ (u32)dns_txid ^ 0x31C3u) | 0xC000u);
        if ((dns_udp_sport & 0x3FFFu) == 53u)
            dns_udp_sport ^= 0x0400u;

        dns_rx_armed = 1;
        udp_send(q, qh, dnsd, 53);

{
            u32 t0 = timer_ticks();
            while ((int)(timer_ticks() - t0) < 8000) {
                spin_rx(256);
                if (dns_done)
                    break;
                sleep_ms(1);
            }
        }

        if (!dns_done) {
            u32 t1 = timer_ticks();
            dns_rx_armed = 1;
            while ((int)(timer_ticks() - t1) < 4000) {
                spin_rx(128);
                if (dns_done) break;
                sleep_ms(1);
            }
            dns_rx_armed = 0;
        } else {
            dns_rx_armed = 0;
        }

        if (!dns_done || dns_len < 20)
            continue;

        {
            const u8 *u = dns_pkt;
            int off = 12;
            u16 tid = (u16)(((u16)u[0] << 8) | (u16)u[1]);
            int an, qd, qi;
            if (tid != dns_txid)
                continue;
            qd = (((int)u[4] << 8) | (int)u[5]);
            an = (((int)u[6] << 8) | (int)u[7]);
            if (an < 1 || qd < 1)
                continue;
            for (qi = 0; qi < qd && qi < 8; qi++) {
                off = dns_skip_name(u, dns_len, off);
                if (off < 0)
                    goto dns_attempt_fail;
                if (off + 4 > dns_len)
                    goto dns_attempt_fail;
                off += 4;
            }
            
            for (i = 0; i < an && i < 32 && off + 10 <= dns_len; i++) {
                off = dns_skip_name(u, dns_len, off);
                if (off < 0)
                    goto dns_attempt_fail;
                if (off + 10 > dns_len)
                    goto dns_attempt_fail;
                {
                    u16 typ = (u16)(((u16)u[off] << 8) | (u16)u[off + 1]);
                    u16 rdlen = (u16)(((u16)u[off + 8] << 8) | (u16)u[off + 9]);
                    if (typ == 1 && rdlen == 4) {
                        if (off + 14 > dns_len)
                            goto dns_attempt_fail;
                        out_ip[0] = u[off + 10];
                        out_ip[1] = u[off + 11];
                        out_ip[2] = u[off + 12];
                        out_ip[3] = u[off + 13];
                        goto dns_lookup_ok;
                    }
                    off += 10 + (int)rdlen;
                }
            }
            if (dns_try_slirp_a(u, dns_len, out_ip) == 0)
                goto dns_lookup_ok;
        }
    dns_attempt_fail:
        (void)0;
    }

kprintf("[dns] calls=%d armed=%d src=%d port=%d qr=%d tid=%d match=%d\n",
        dbg_udp_calls, dbg_udp_drop_armed, dbg_udp_drop_src,
        dbg_udp_drop_port, dbg_udp_drop_qr, dbg_udp_drop_tid, dbg_udp_matched);
    kprintf("[dns] last sport=0x%x txid=0x%x\n",
        (u32)dns_udp_sport, (u32)dns_txid);
    return -1;
dns_lookup_ok:
    dns_rx_armed = 0;
    return 0;
}

static int http_body_offset(const u8 *buf, int len) {
    int i;
    for (i = 0; i + 3 < len; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n')
            return i + 4;
    }
    for (i = 0; i + 1 < len; i++) {
        if (buf[i] == '\n' && buf[i + 1] == '\n')
            return i + 2;
    }
    return -1;
}

static int http_headers_complete(const u8 *buf, int len) {
    return http_body_offset(buf, len) >= 0;
}

int net_http_get(const char *host, const char *path, char *out, int out_cap) {
    u8 dip[4];
    u32 t0;
    int i;
    u8 req[1536];
    int rl;
    char *hp;

    if (!s_net_ok || out_cap < 8)
        return -1;
    ip_tcp_rasm_len = 0;
    ip_udp_rasm_len = 0;
    if (!s_net_ok || out_cap < 8)
        return -1;
    

    tcp_state   = T_IDLE;
    tcp_asm_len = 0;
    ip_tcp_rasm_len = 0;
    ip_udp_rasm_len = 0;
    dns_rx_armed = 0;
    dns_done    = 0;    
    if (net_dns_lookup(host, dip) != 0)
        return -2;

    for (i = 0; i < 4; i++) tcp_dst[i] = dip[i];
    
    tcp_dport = 80;
static u16 sport_counter = 0;
    sport_counter++;
static u16 s_sport_seq = 0;
    s_sport_seq += 137u;
tcp_sport   = (u16)(0xC000u + (((u32)timer_ticks() + s_sport_seq) & 0x1FFFu));
tcp_iss     = timer_ticks() ^ ((u32)sport_counter * 0x6B5FD7C7u) ^ 0xBA5E0000u;
tcp_snd_nxt = tcp_iss + 1u;    tcp_asm_len = 0;
tcp_state   = T_IDLE;   

tcp_asm_len = 0;

tcp_state   = T_SYN_SENT;
    tcp_send(tcp_iss, 0, 0x02, 0, 0);

    t0 = timer_ticks();
    while ((int)(timer_ticks() - t0) < 8000 && tcp_state == T_SYN_SENT) {
        spin_rx(400);
        sleep_ms(1);
    }

    if (tcp_state != T_ESTAB)
        return -3;

    tcp_send(tcp_iss + 1u, tcp_rcv_nxt, 0x10, 0, 0);

    rl = ksnprintf((char *)req, (int)sizeof(req),
                   "GET %s HTTP/1.0\r\n"
                   "Host: %s\r\n"
                   "Connection: close\r\n"
                   "User-Agent: Mozilla/5.0 (compatible; SavaOS)\r\n"
                   "Accept: text/html,*/*\r\n"
                   "\r\n",
                   path, host);
    if (rl < 16 || rl >= (int)sizeof(req) - 1)
        return -1;

    tcp_send(tcp_iss + 1u, tcp_rcv_nxt, 0x18, req, rl);
    tcp_snd_nxt = tcp_iss + 1u + (u32)rl;

    {
        int ms_after_hdr = 0;
        t0 = timer_ticks();
        while ((int)(timer_ticks() - t0) < 20000) {
            spin_rx(500);
            if (tcp_state == T_IDLE)
                break;
            if (tcp_asm_len >= TCP_ASM_MAX - 2048)
                break;
            if (http_headers_complete(tcp_asm, tcp_asm_len)) {
                ms_after_hdr++;
                if (ms_after_hdr > 500)
                    break;
            } else
                ms_after_hdr = 0;
            if (tcp_state == T_FIN)
                break;
            sleep_ms(1);
        }
    }

    tcp_send(tcp_snd_nxt, tcp_rcv_nxt, 0x11, 0, 0);
    for (i = 0; i < 100; i++) {
        spin_rx(200);
        sleep_ms(1);
    }

    if (tcp_asm_len <= 0)
        return -4;
    tcp_asm[tcp_asm_len] = 0;
    {
        int off = http_body_offset(tcp_asm, tcp_asm_len);
        if (off < 0)
            return -5;
        hp = (char *)(tcp_asm + off);
    }
    {
        int body = tcp_asm_len - (int)(hp - (char *)tcp_asm);
        if (body < 0)
            return -1;
        if (body == 0) {
            out[0] = 0;
            return 0;
        }
        if (body > out_cap - 1)
            body = out_cap - 1;
        kmemcpy(out, hp, (size_t)body);
        out[body] = 0;
        return body;
    }
}
