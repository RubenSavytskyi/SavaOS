

#include "app_browser.h"
#include "keyboard.h"
#include "net.h"
#include "string.h"
#include "sv_desktop_internal.h"
#include "sv_gfx.h"
#include "timer.h"
extern int ui_caret_blink_fast;

#define FONT_W          6
#define FONT_H          8
#define TOOLBAR_H       28          
#define STATUSBAR_H     (FONT_H + 5)
#define SCROLLBAR_W     11
#define FETCH_CAP       (40 * 1024)   
#define MAX_LINES       256
#define LINE_W          54          
#define MAX_LINKS       128
#define HIST_MAX        16
#define ADDR_MAXLEN     256
#define LINK_COLOR      0x03        
#define LINK_HOVER      0x0B        

typedef struct {
    int  line;          
    int  col;           
    int  len;           
    char href[200];     
} link_t;

typedef struct {
    
    char addr[ADDR_MAXLEN];
    int  addr_cursor;
    int  addr_focused;  

    
    char host[128];
    char path[200];

    
    char  lines[MAX_LINES][LINE_W + 1];
    int   line_count;

    
    link_t links[MAX_LINKS];
    int    link_count;
    int    hovered_link;   

    
    int  scroll_y;
    int  sb_dragging;      
    int  sb_drag_start_y;
    int  sb_drag_start_scroll;

    
    char history[HIST_MAX][ADDR_MAXLEN];
    int  hist_pos;         
    int  hist_len;         

    
    char status[128];
    int  loading;
} brow_state_t;

static brow_state_t st[MAX_WIN];
static char fetch_buf[FETCH_CAP];

static const char HOME_ADDR[] = "http://theoldnet.com/";

static const char icon[24][25] = {
    "........................",
    "........................",
    "........BBBBBBB.........",
    ".....BBBBWWBWWBBBB......",
    "...BBWWBWWWBWWWBWWBB....",
    "...BWWWBWWWBWWWBWWWB....",
    "..BWWWBWWWWBWWWWBWWWB...",
    "..BWWWBWWWWBWWWWBWWWB...",
    "..BBBBBBBBBBBBBBBBBBB...",
    ".BWWWBWWWWWBWWWWWBWWWB..",
    ".BWWWBWWWWWBWWWWWBWWWB..",
    ".BWWWBWWWWWBWWWWWBWWWB..",
    ".BBBBBBBBBBBBBBBBBBBBB..",
    ".BWWWBWWWWWBWWWWWBWWWB..",
    ".BWWWBWWWWWBWWWWWBWWWB..",
    ".BWWWBWWWWWBWWWWWBWWWB..",
    "..BBBBBBBBBBBBBBBBBBB...",
    "..BWWWBWWWWBWWWWBWWWB...",
    "..BWWWBWWWWBWWWWBWWWB...",
    "...BWWWBWWWBWWWBWWWB....",
    "...BBWWBWWWBWWWBWWBB....",
    ".....BBBBWWBWWBBBB......",
    "........BBBBBBB.........",
    "........................"
};

extern int   str_starts_with(const char *s, const char *pfx);
extern int   str_eq(const char *a, const char *b);
extern int   str_len(const char *s);
extern void  str_cpy(char *dst, const char *src, int cap);
extern int   ksnprintf(char *buf, int size, const char *fmt, ...);

static int brow_str_ncpy(char *dst, const char *src, int n) {
    int i = 0;
    while (i < n - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
    return i;
}

static int brow_to_lower(int c) {
    if (c >= 'A' && c <= 'Z') return c + 32;
    return c;
}

static int brow_istarts(const char *s, const char *pfx) {
    while (*pfx) {
        if (brow_to_lower((unsigned char)*s) != brow_to_lower((unsigned char)*pfx))
            return 0;
        s++; pfx++;
    }
    return 1;
}

static const char *brow_imemstr(const char *s, int slen, const char *needle) {
    int nl = str_len(needle);
    int i;
    if (nl == 0) return s;
    for (i = 0; i <= slen - nl; i++) {
        int j = 0;
        while (j < nl && brow_to_lower((unsigned char)s[i+j]) ==
                         brow_to_lower((unsigned char)needle[j])) j++;
        if (j == nl) return s + i;
    }
    return 0;
}

static int parse_url(const char *url, char *host, int hostsz,
                     char *path, int pathsz) {
    const char *p = url;
    if (brow_istarts(p, "http://"))  p += 7;
    else if (brow_istarts(p, "https://")) p += 8;  
    else return 0;

    
    int hi = 0;
    while (*p && *p != '/' && *p != ':' && hi < hostsz - 1)
        host[hi++] = *p++;
    host[hi] = 0;
    if (!host[0]) return 0;

    
    if (*p == ':') { while (*p && *p != '/') p++; }

    
    if (!*p) {
        brow_str_ncpy(path, "/", pathsz);
    } else {
        brow_str_ncpy(path, p, pathsz);
    }
    return 1;
}

static void resolve_href(int id, const char *href, char *out, int outsz) {
    
    if (brow_istarts(href, "http://") || brow_istarts(href, "https://")) {
        brow_str_ncpy(out, href, outsz);
        return;
    }
    
    if (href[0] == '/') {
        ksnprintf(out, outsz, "http://%s%s", st[id].host, href);
        return;
    }
    
    char base[200];
    brow_str_ncpy(base, st[id].path, (int)sizeof(base));
    
    int bi = str_len(base) - 1;
    while (bi > 0 && base[bi] != '/') bi--;
    base[bi + 1] = 0;
    ksnprintf(out, outsz, "http://%s%s%s", st[id].host, base, href);
}

#define EMIT(ch) do {                                   \
    if (li >= MAX_LINES) goto done_emit;                \
    if (col >= LINE_W) {                                \
        lines[li][col] = 0; li++; col = 0;             \
        if (li >= MAX_LINES) goto done_emit;            \
    }                                                   \
    lines[li][col++] = (char)(ch);                      \
} while(0)

#define NEWLINE() do {                                  \
    if (li < MAX_LINES) { lines[li][col] = 0; }        \
    li++; col = 0;                                      \
} while(0)

static int html_render(const char *html,
                       char lines[][LINE_W + 1], int max_lines,
                       link_t *links, int *link_count_out)
{
    int li = 0, col = 0;
    int lc = 0;            
    int last_sp = 1;
    int in_tag = 0;
    int in_script = 0;
    int in_style  = 0;
    char tag_buf[128];
    int  tag_len  = 0;
    int  in_anchor = 0;
    char cur_href[200];
    int  link_line_start = 0, link_col_start = 0;
    char link_text_buf[200];
    int  link_text_len = 0;

    
    if (max_lines > 0) lines[0][0] = 0;

    while (*html) {
        unsigned char c = (unsigned char)*html;

        
        if (in_tag) {
            if (c == '>') {
                in_tag = 0;
                tag_buf[tag_len < 127 ? tag_len : 127] = 0;
                const char *t = tag_buf;
                
                int closing = (*t == '/');
                if (closing) t++;

                
                if (brow_istarts(t, "script")) {
                    in_script = !closing;
                }
                if (brow_istarts(t, "style")) {
                    in_style = !closing;
                }

                
                if (brow_istarts(t, "p")  || brow_istarts(t, "/p") ||
                    brow_istarts(t, "h1") || brow_istarts(t, "h2") ||
                    brow_istarts(t, "h3") || brow_istarts(t, "h4") ||
                    brow_istarts(t, "h5") || brow_istarts(t, "h6") ||
                    brow_istarts(t, "div") || brow_istarts(t, "/div") ||
                    brow_istarts(t, "tr") || brow_istarts(t, "/tr") ||
                    brow_istarts(t, "li")) {
                    if (col > 0) { NEWLINE(); }
                    last_sp = 1;
                }
                
                if (brow_istarts(t, "br")) {
                    NEWLINE();
                    last_sp = 1;
                }

                
                if (!closing && brow_istarts(t, "a ")) {
                    
                    const char *hp = brow_imemstr(t, str_len(t), "href=");
                    if (hp) {
                        hp += 5;
                        char q = *hp;
                        if (q == '"' || q == '\'') {
                            hp++;
                            int hi2 = 0;
                            while (*hp && *hp != q && hi2 < 199)
                                cur_href[hi2++] = *hp++;
                            cur_href[hi2] = 0;
                        } else {
                            int hi2 = 0;
                            while (*hp && *hp != ' ' && *hp != '>' && hi2 < 199)
                                cur_href[hi2++] = *hp++;
                            cur_href[hi2] = 0;
                        }
                        in_anchor = 1;
                        link_line_start = li;
                        link_col_start  = col;
                        link_text_len   = 0;
                        link_text_buf[0] = 0;
                    }
                }
                
                if (closing && brow_istarts(t, "a") && in_anchor && lc < MAX_LINKS) {
                    
                    int worthy = (brow_istarts(cur_href, "http") ||
                                  cur_href[0] == '/' || cur_href[0] == '.');
                    if (worthy && link_text_len > 0) {
                        links[lc].line = link_line_start;
                        links[lc].col  = link_col_start;
                        links[lc].len  = link_text_len;
                        brow_str_ncpy(links[lc].href, cur_href, 200);
                        lc++;
                    }
                    in_anchor = 0;
                    cur_href[0] = 0;
                }
            } else {
                if (tag_len < 127) tag_buf[tag_len++] = (char)c;
                html++;
                continue;
            }
            html++;
            continue;
        }

        
        if (c == '<') {
            in_tag = 1;
            tag_len = 0;
            html++;
            continue;
        }

        
        if (in_script || in_style) { html++; continue; }

        
        if (c == '&') {
            unsigned char ec = 0;
            if (str_starts_with(html, "&nbsp;"))  { html += 6; ec = ' '; }
            else if (str_starts_with(html, "&lt;"))   { html += 4; ec = '<'; }
            else if (str_starts_with(html, "&gt;"))   { html += 4; ec = '>'; }
            else if (str_starts_with(html, "&amp;"))  { html += 5; ec = '&'; }
            else if (str_starts_with(html, "&quot;")) { html += 6; ec = '"'; }
            else if (str_starts_with(html, "&apos;")) { html += 6; ec = '\''; }
            else if (str_starts_with(html, "&#")) {
                
                html++;
                while (*html && *html != ';' && *html != '<') html++;
                if (*html == ';') html++;
                continue;
            } else {
                html++;
                while (*html && *html != ';' && *html != '<') html++;
                if (*html == ';') html++;
                continue;
            }
            if (ec == ' ') {
                if (!last_sp && col < LINE_W) { EMIT(' '); last_sp = 1; }
            } else if (ec >= 32 && ec < 127) {
                EMIT(ec);
                if (in_anchor && link_text_len < 199) link_text_buf[link_text_len++] = (char)ec;
                last_sp = 0;
            }
            continue;
        }

        
        html++;
        if (c == '\n' || c == '\r') {
            if (c == '\r' && *html == '\n') html++;
            if (!last_sp) { EMIT(' '); last_sp = 1; }
            continue;
        }
        if (c == '\t' || c == ' ') {
            if (!last_sp && col > 0) { EMIT(' '); last_sp = 1; }
            continue;
        }
        if (c >= 32 && c < 127) {
            EMIT(c);
            if (in_anchor && link_text_len < 199) link_text_buf[link_text_len++] = (char)c;
            last_sp = 0;
        }
    }
done_emit:
    if (col > 0 && li < max_lines) { lines[li][col] = 0; li++; }
    if (li == 0 && max_lines > 0) {
        brow_str_ncpy(lines[0], "(empty page)", LINE_W + 1);
        li = 1;
    }
    if (link_count_out) *link_count_out = lc;
    return li < max_lines ? li : max_lines;
}

static void hist_push(int id, const char *url) {
    
    st[id].hist_len = st[id].hist_pos + 1;
    if (st[id].hist_len > HIST_MAX) {
        
        int i;
        for (i = 0; i < HIST_MAX - 1; i++)
            brow_str_ncpy(st[id].history[i], st[id].history[i+1], ADDR_MAXLEN);
        st[id].hist_len = HIST_MAX;
        st[id].hist_pos = HIST_MAX - 1;
    } else {
        st[id].hist_pos = st[id].hist_len - 1;
    }
    brow_str_ncpy(st[id].history[st[id].hist_pos], url, ADDR_MAXLEN);
}

static void brow_set_status(int id, const char *s) {
    brow_str_ncpy(st[id].status, s, (int)sizeof(st[id].status));
}

static void brow_navigate(int id, const char *url, int push_hist);

static int dechunk(char *buf, int len) {
    char *src = buf;
    char *end = buf + len;
    char *dst = buf;

    while (src < end) {
        
        unsigned long sz = 0;
        int got = 0;
        while (src < end) {
            char c = *src;
            if      (c >= '0' && c <= '9') { sz = sz * 16 + (unsigned long)(c - '0');      src++; got++; }
            else if (c >= 'a' && c <= 'f') { sz = sz * 16 + (unsigned long)(c - 'a' + 10); src++; got++; }
            else if (c >= 'A' && c <= 'F') { sz = sz * 16 + (unsigned long)(c - 'A' + 10); src++; got++; }
            else break;
        }
        if (!got) break;
        
        while (src < end && *src != '\n') src++;
        if (src < end) src++;   
        if (sz == 0) break;     
        
        if (src + (int)sz > end) sz = (unsigned long)(end - src);
        if (dst != src) {
            int i;
            for (i = 0; i < (int)sz; i++) dst[i] = src[i];
        }
        dst += sz;
        src += sz;
        
        if (src < end && *src == '\r') src++;
        if (src < end && *src == '\n') src++;
    }
    *dst = 0;
    return (int)(dst - buf);
}

static void brow_load(int id) {
    int n = 0;
    st[id].loading = 1;
    brow_set_status(id, "Loading...");
    desktop_needs_full_blit = 1;

    if (!net_ready()) {
        brow_set_status(id, "No network (RTL8139 + QEMU -netdev user)");
        st[id].loading = 0;
        brow_str_ncpy(fetch_buf, "<p>No network available.</p>", FETCH_CAP);
        n = str_len(fetch_buf);
    } else {
        

        n = net_http_get(st[id].host, st[id].path, fetch_buf,
                         (int)sizeof(fetch_buf) - 1);
        if (n < 0) {
            const char *err;
            if      (n == -2) err = "DNS failed";
            else if (n == -3) err = "TCP connect failed";
            else if (n == -4) err = "No data received";
            else if (n == -5) err = "Bad HTTP response";
            else              err = "Network error";
            brow_set_status(id, err);
            st[id].loading = 0;
            ksnprintf(fetch_buf, FETCH_CAP, "<p><b>Error:</b> %s</p>", err);
            n = str_len(fetch_buf);
        } else {
            fetch_buf[n] = 0;
            

            char c0 = fetch_buf[0];
            int looks_chunked = ((c0 >= '0' && c0 <= '9') ||
                                 (c0 >= 'a' && c0 <= 'f') ||
                                 (c0 >= 'A' && c0 <= 'F'));
            if (looks_chunked) {
                n = dechunk(fetch_buf, n);
            }
            char tmp[80];
            ksnprintf(tmp, (int)sizeof(tmp), "OK — %d bytes", n);
            brow_set_status(id, tmp);
        }
    }

    fetch_buf[n < FETCH_CAP - 1 ? n : FETCH_CAP - 1] = 0;
    st[id].line_count  = html_render(fetch_buf,
                                      st[id].lines, MAX_LINES,
                                      st[id].links, &st[id].link_count);
    st[id].scroll_y    = 0;
    st[id].hovered_link = -1;
    st[id].loading     = 0;
    desktop_needs_full_blit = 1;
}

static void brow_navigate(int id, const char *url, int push_hist) {
    if (id < 0 || id >= MAX_WIN) return;

    
    char full[ADDR_MAXLEN];
    if (!brow_istarts(url, "http://") && !brow_istarts(url, "https://")) {
        ksnprintf(full, (int)sizeof(full), "http://%s", url);
        url = full;
    }

    if (!parse_url(url, st[id].host, (int)sizeof(st[id].host),
                        st[id].path, (int)sizeof(st[id].path))) {
        brow_set_status(id, "Invalid URL");
        desktop_needs_full_blit = 1;
        return;
    }

    brow_str_ncpy(st[id].addr, url, ADDR_MAXLEN);
    st[id].addr_cursor = str_len(st[id].addr);

    if (push_hist) hist_push(id, url);

    brow_load(id);
}

static void brow_back(int id) {
    if (st[id].hist_pos > 0) {
        st[id].hist_pos--;
        brow_navigate(id, st[id].history[st[id].hist_pos], 0);
    }
}

static void brow_forward(int id) {
    if (st[id].hist_pos < st[id].hist_len - 1) {
        st[id].hist_pos++;
        brow_navigate(id, st[id].history[st[id].hist_pos], 0);
    }
}

static void sb_geometry(int id, int cy, int cw, int ch,
                         int *sx_out, int *sy_out, int *sh_out,
                         int *thumb_y_out, int *thumb_h_out,
                         int *visible_out, int *max_scroll_out)
{
    int pad      = 3;
    int body_top = cy + TOOLBAR_H + pad;
    int body_h   = ch - TOOLBAR_H - pad - STATUSBAR_H;
    if (body_h < FONT_H) body_h = FONT_H;

    int visible   = body_h / FONT_H;
    if (visible < 1) visible = 1;
    int max_sc    = st[id].line_count - visible;
    if (max_sc < 0) max_sc = 0;
    if (st[id].scroll_y > max_sc) st[id].scroll_y = max_sc;
    if (st[id].scroll_y < 0)      st[id].scroll_y = 0;

    int sx = cw - SCROLLBAR_W;   
    int sy = body_top;
    int sh = body_h;

    int th, ty;
    if (st[id].line_count <= visible) {
        th = sh - 22;
        ty = sy + 11;
    } else {
        th = (visible * (sh - 22)) / st[id].line_count;
        if (th < 12) th = 12;
        ty = sy + 11 + (st[id].scroll_y * (sh - 22 - th)) / max_sc;
    }

    if (sx_out)        *sx_out        = sx;
    if (sy_out)        *sy_out        = sy;
    if (sh_out)        *sh_out        = sh;
    if (thumb_y_out)   *thumb_y_out   = ty;
    if (thumb_h_out)   *thumb_h_out   = th;
    if (visible_out)   *visible_out   = visible;
    if (max_scroll_out)*max_scroll_out = max_sc;
}

static void draw(int id, int cx, int cy, int cw, int ch) {
    if (id < 0 || id >= MAX_WIN) return;

    int pad      = 3;
    int body_top = cy + TOOLBAR_H + pad;
    int body_h   = ch - TOOLBAR_H - pad - STATUSBAR_H;
    if (body_h < FONT_H) body_h = FONT_H;

    int sx, sy, sh, thumb_y, thumb_h, visible, max_sc;
    sb_geometry(id, cy, cw, ch, &sx, &sy, &sh, &thumb_y, &thumb_h, &visible, &max_sc);

    int content_w = cw - SCROLLBAR_W - pad;

    
    vga13_fill_rect(cx, cy, cw, ch, VGA13_WHITE);

    
    vga13_fill_rect(cx, cy, cw, TOOLBAR_H, PAL_LIGHT_GRAY);
    hline_px(cx, cx + cw - 1, cy + TOOLBAR_H - 1, VGA13_BLACK);

    
    int btn_w = 18, btn_h = 14, btn_y = cy + 7;

int bx = cx + pad;

vga13_fill_rect(bx, btn_y, btn_w, btn_h, VGA13_WHITE);

hline_px(bx, bx + btn_w - 1, btn_y, VGA13_BLACK);
hline_px(bx, bx + btn_w - 1, btn_y + btn_h - 1, VGA13_BLACK);
vline_px(bx, btn_y, btn_y + btn_h - 1, VGA13_BLACK);
vline_px(bx + btn_w - 1, btn_y, btn_y + btn_h - 1, VGA13_BLACK);

vga13_draw_string(bx + 7, btn_y + 3, "<",
                  VGA13_BLACK, VGA13_WHITE, 0);

int fx = bx + btn_w + 2;

vga13_fill_rect(fx, btn_y, btn_w, btn_h, VGA13_WHITE);

hline_px(fx, fx + btn_w - 1, btn_y, VGA13_BLACK);
hline_px(fx, fx + btn_w - 1, btn_y + btn_h - 1, VGA13_BLACK);
vline_px(fx, btn_y, btn_y + btn_h - 1, VGA13_BLACK);
vline_px(fx + btn_w - 1, btn_y, btn_y + btn_h - 1, VGA13_BLACK);

vga13_draw_string(fx + 7, btn_y + 3, ">",
                  VGA13_BLACK, VGA13_WHITE, 0);

int addr_x = cx + pad + 2 * (btn_w + 2) + 2;
int addr_w = cw - (addr_x - cx) - pad;

int addr_bg = VGA13_WHITE;

vga13_fill_rect(addr_x, btn_y, addr_w, btn_h, addr_bg);

hline_px(addr_x, addr_x + addr_w - 1,
         btn_y, VGA13_BLACK);

hline_px(addr_x, addr_x + addr_w - 1,
         btn_y + btn_h - 1, VGA13_BLACK);

vline_px(addr_x,
         btn_y, btn_y + btn_h - 1,
         VGA13_BLACK);

vline_px(addr_x + addr_w - 1,
         btn_y, btn_y + btn_h - 1,
         VGA13_BLACK);

{
    char disp[64];

    int max_chars = (addr_w - 4) / FONT_W;

    if (max_chars < 1)
        max_chars = 1;

    if (max_chars > 63)
        max_chars = 63;

    int alen = str_len(st[id].addr);

    int start = alen - max_chars;

    if (start < 0)
        start = 0;

    brow_str_ncpy(disp,
                  st[id].addr + start,
                  max_chars + 1);

    vga13_draw_string(addr_x + 2,
                      btn_y + 3,
                      disp,
                      VGA13_BLACK,
                      addr_bg,
                      0);

    
    if (st[id].addr_focused) {

        int cur_col = alen - start;

        if (cur_col > max_chars)
            cur_col = max_chars;

        int cur_px = addr_x + 2 + cur_col * FONT_W;

        {
            u32 phase = (u32)(ui_caret_blink_fast ? 40u : 120u);

            if (((u32)timer_ticks() / phase) & 1u)
                vline_px(cur_px,
                         btn_y + 2,
                         btn_y + btn_h - 3,
                         VGA13_BLACK);
        }
    } 
}

    
    vga13_fill_rect(cx + pad, body_top, content_w, body_h, VGA13_WHITE);

    
    int max_chars_vis = (content_w - 3) / FONT_W;
    if (max_chars_vis < 1) max_chars_vis = 1;
    if (max_chars_vis > LINE_W) max_chars_vis = LINE_W;

    int draw_y = body_top + 1;
    int i, line;
    for (i = 0, line = st[id].scroll_y;
         i < visible && line < st[id].line_count;
         i++, line++)
    {
        int lk;
        int last_col = 0;
        int base_x   = cx + pad + 1;
        int linelen  = str_len(st[id].lines[line]);
        if (linelen > max_chars_vis) linelen = max_chars_vis;

        for (lk = 0; lk < st[id].link_count; lk++) {
            if (st[id].links[lk].line != line) continue;

            int lc_start = st[id].links[lk].col;
            int lc_end   = lc_start + st[id].links[lk].len;
            if (lc_start > max_chars_vis) continue;
            if (lc_end   > max_chars_vis) lc_end = max_chars_vis;

            
            if (lc_start > last_col) {
                char seg[LINE_W + 1];
                int  seglen = lc_start - last_col;
                if (seglen > max_chars_vis - last_col) seglen = max_chars_vis - last_col;
                if (seglen > 0) {
                    brow_str_ncpy(seg, st[id].lines[line] + last_col, seglen + 1);
                    vga13_draw_string(base_x + last_col * FONT_W, draw_y,
                                      seg, VGA13_BLACK, VGA13_WHITE, 0);
                }
            }

            
            int seglen = lc_end - lc_start;
            if (seglen > 0) {
                char seg[LINE_W + 1];
                brow_str_ncpy(seg, st[id].lines[line] + lc_start, seglen + 1);
                int lc_col = (lk == st[id].hovered_link) ? LINK_HOVER : LINK_COLOR;
                hline_px(base_x + lc_start * FONT_W,
                         base_x + lc_end   * FONT_W - 1,
                         draw_y + FONT_H - 1, lc_col);
                vga13_draw_string(base_x + lc_start * FONT_W, draw_y,
                                  seg, lc_col, VGA13_WHITE, 0);
            }
            last_col = lc_end;
        }

        
        if (last_col < linelen) {
            char seg[LINE_W + 1];
            int  seglen = linelen - last_col;
            brow_str_ncpy(seg, st[id].lines[line] + last_col, seglen + 1);
            vga13_draw_string(base_x + last_col * FONT_W, draw_y,
                              seg, VGA13_BLACK, VGA13_WHITE, 0);
        }

        draw_y += FONT_H;
    }

    
    
    vga13_fill_rect(cx + sx, sy, SCROLLBAR_W, sh, PAL_LIGHT_GRAY);
    hline_px(cx + sx, cx + sx + SCROLLBAR_W - 1, sy,      VGA13_BLACK);
    hline_px(cx + sx, cx + sx + SCROLLBAR_W - 1, sy + sh - 1, VGA13_BLACK);
    vline_px(cx + sx,                sy, sy + sh - 1, VGA13_BLACK);
    vline_px(cx + sx + SCROLLBAR_W - 1, sy, sy + sh - 1, VGA13_BLACK);

    
    vga13_fill_rect(cx + sx + 1, sy + 1, SCROLLBAR_W - 2, 10, VGA13_WHITE);
    hline_px(cx + sx + 1, cx + sx + SCROLLBAR_W - 2, sy + 1,  VGA13_BLACK);
    hline_px(cx + sx + 1, cx + sx + SCROLLBAR_W - 2, sy + 10, VGA13_BLACK);
    vline_px(cx + sx + 1, sy + 1, sy + 10, VGA13_BLACK);
    vline_px(cx + sx + SCROLLBAR_W - 2, sy + 1, sy + 10, VGA13_BLACK);
    vga13_draw_string(cx + sx + 3, sy + 2, "\x1E", VGA13_BLACK, VGA13_WHITE, 0);

    
    vga13_fill_rect(cx + sx + 1, sy + sh - 11, SCROLLBAR_W - 2, 10, VGA13_WHITE);
    hline_px(cx + sx + 1, cx + sx + SCROLLBAR_W - 2, sy + sh - 11, VGA13_BLACK);
    hline_px(cx + sx + 1, cx + sx + SCROLLBAR_W - 2, sy + sh - 1,  VGA13_BLACK);
    vline_px(cx + sx + 1, sy + sh - 11, sy + sh - 1, VGA13_BLACK);
    vline_px(cx + sx + SCROLLBAR_W - 2, sy + sh - 11, sy + sh - 1, VGA13_BLACK);
    vga13_draw_string(cx + sx + 3, sy + sh - 10, "\x1F", VGA13_BLACK, VGA13_WHITE, 0);

    
    vga13_fill_rect(cx + sx + 2, thumb_y, SCROLLBAR_W - 4, thumb_h, PAL_LIGHT_GRAY);
    hline_px(cx + sx + 2, cx + sx + SCROLLBAR_W - 3, thumb_y,              VGA13_BLACK);
    hline_px(cx + sx + 2, cx + sx + SCROLLBAR_W - 3, thumb_y + thumb_h - 1, VGA13_BLACK);
    vline_px(cx + sx + 2, thumb_y, thumb_y + thumb_h - 1, VGA13_BLACK);
    vline_px(cx + sx + SCROLLBAR_W - 3, thumb_y, thumb_y + thumb_h - 1, VGA13_BLACK);
    hline_px(cx + sx + 3, cx + sx + SCROLLBAR_W - 4, thumb_y + 1, VGA13_WHITE);
    vline_px(cx + sx + 3, thumb_y + 1, thumb_y + thumb_h - 2, VGA13_WHITE);

    
    vline_px(cx + sx - 1, body_top, body_top + body_h - 1, PAL_DARK_GRAY);

    
    {
        int status_y = cy + ch - STATUSBAR_H;
        vga13_fill_rect(cx, status_y, cw, STATUSBAR_H, PAL_LIGHT_GRAY);
        hline_px(cx, cx + cw - 1, status_y, VGA13_BLACK);
        vga13_draw_string(cx + pad + 1, status_y + 2,
                          st[id].status, VGA13_BLACK, PAL_LIGHT_GRAY, 0);
    }
}

static void on_open(int id) {
    if (id < 0 || id >= MAX_WIN) return;
    st[id].scroll_y      = 0;
    st[id].hovered_link  = -1;
    st[id].sb_dragging   = 0;
    st[id].hist_pos      = -1;
    st[id].hist_len      = 0;
    st[id].addr_focused  = 0;
    st[id].addr[0]       = 0;
    st[id].addr_cursor   = 0;
    st[id].link_count    = 0;
    st[id].line_count    = 0;
    

    if (!net_ready()) {
        brow_set_status(id, "Initializing network...");
        desktop_needs_full_blit = 1;
        net_init();  

    }
    brow_set_status(id, net_ready() ? "Ready" : "No NIC");
    brow_navigate(id, HOME_ADDR, 1);
}

static void on_key(int id, int key) {
    if (id < 0 || id >= MAX_WIN) return;

    
    if (key == KEY_TAB) {
        st[id].addr_focused ^= 1;
        desktop_needs_full_blit = 1;
        return;
    }

    
    if (st[id].addr_focused) {
        int alen = str_len(st[id].addr);
        if (key == KEY_ENTER) {
            st[id].addr_focused = 0;
            brow_navigate(id, st[id].addr, 1);
        } else if (key == KEY_BACKSP || key == KEY_DELETE) {
            if (alen > 0) { st[id].addr[alen - 1] = 0; }
            desktop_needs_full_blit = 1;
        } else if (key == KEY_ESC) {
            st[id].addr_focused = 0;
            desktop_needs_full_blit = 1;
        } else if (key >= 32 && key <= 126) {
            if (alen < ADDR_MAXLEN - 1) {
                st[id].addr[alen]     = (char)key;
                st[id].addr[alen + 1] = 0;
            }
            desktop_needs_full_blit = 1;
        }
        return;
    }

    
    if (key == KEY_F5) {
        brow_navigate(id, st[id].addr, 0);
        return;
    }
    

    if (key == '[') { brow_back(id);    return; }
    if (key == ']') { brow_forward(id); return; }

    
    if (key == KEY_UP)   { if (st[id].scroll_y > 0) st[id].scroll_y--; desktop_needs_full_blit = 1; return; }
    if (key == KEY_DOWN) { st[id].scroll_y++; desktop_needs_full_blit = 1; return; }
    if (key == KEY_PGUP) { st[id].scroll_y -= 8; if (st[id].scroll_y < 0) st[id].scroll_y = 0; desktop_needs_full_blit = 1; return; }
    if (key == KEY_PGDN) { st[id].scroll_y += 8; desktop_needs_full_blit = 1; return; }
    if (key == KEY_HOME) { st[id].scroll_y = 0;  desktop_needs_full_blit = 1; return; }
    if (key == KEY_END)  { st[id].scroll_y = st[id].line_count; desktop_needs_full_blit = 1; return; }
}

static void on_click(int id, app_mouse_t *ev) {
    if (id < 0 || id >= MAX_WIN || !ev) return;
    if (ev->button != 0) return;

    int pad      = 3;
    int btn_w    = 18, btn_h = 14;
    int btn_y_rel = 7;   

    
    int rx = ev->mx - ev->cx;
    int ry = ev->my - ev->cy;

    
    if (rx >= pad && rx < pad + btn_w && ry >= btn_y_rel && ry < btn_y_rel + btn_h) {
        brow_back(id); return;
    }
    
    int fx = pad + btn_w + 2;
    if (rx >= fx && rx < fx + btn_w && ry >= btn_y_rel && ry < btn_y_rel + btn_h) {
        brow_forward(id); return;
    }
    
    int addr_x_rel = pad + 2 * (btn_w + 2) + 2;
    if (ry >= btn_y_rel && ry < btn_y_rel + btn_h && rx >= addr_x_rel) {
        st[id].addr_focused = 1;
        desktop_needs_full_blit = 1;
        return;
    }
    
    st[id].addr_focused = 0;

    int body_top  = TOOLBAR_H + pad;
    int body_h    = ev->ch - TOOLBAR_H - pad - STATUSBAR_H;
    int sx_rel    = ev->cw - SCROLLBAR_W;

    
    if (rx >= sx_rel && ry >= body_top && ry < body_top + body_h) {
        int sy = body_top;
        int sh = body_h;
        int visible, max_sc, thumb_y, thumb_h;
        sb_geometry(id, ev->cy, ev->cw, ev->ch,
                    0, 0, 0, &thumb_y, &thumb_h, &visible, &max_sc);
        
        thumb_y -= ev->cy;

        int my_rel = ry;  

        
        if (my_rel >= sy + 1 && my_rel <= sy + 10) {
            if (st[id].scroll_y > 0) st[id].scroll_y--;
            desktop_needs_full_blit = 1;
            return;
        }
        
        if (my_rel >= sy + sh - 11 && my_rel <= sy + sh - 1) {
            st[id].scroll_y++;
            desktop_needs_full_blit = 1;
            return;
        }
        
        if (my_rel < thumb_y && my_rel > sy + 10) {
            st[id].scroll_y -= visible;
            if (st[id].scroll_y < 0) st[id].scroll_y = 0;
            desktop_needs_full_blit = 1;
            return;
        }
        
        if (my_rel >= thumb_y + thumb_h && my_rel < sy + sh - 11) {
            st[id].scroll_y += visible;
            desktop_needs_full_blit = 1;
            return;
        }
        
        if (my_rel >= thumb_y && my_rel < thumb_y + thumb_h) {
            st[id].sb_dragging         = 1;
            st[id].sb_drag_start_y     = ev->my;
            st[id].sb_drag_start_scroll = st[id].scroll_y;
            return;
        }
        return;
    }

    
    if (ry >= body_top && ry < body_top + body_h && rx < sx_rel) {
        int row = (ry - body_top) / FONT_H + st[id].scroll_y;
        int col = (rx - pad - 1) / FONT_W;
        int lk;
        for (lk = 0; lk < st[id].link_count; lk++) {
            if (st[id].links[lk].line == row &&
                col >= st[id].links[lk].col &&
                col <  st[id].links[lk].col + st[id].links[lk].len)
            {
                char resolved[ADDR_MAXLEN];
                resolve_href(id, st[id].links[lk].href, resolved, ADDR_MAXLEN);
                brow_navigate(id, resolved, 1);
                return;
            }
        }
    }
    desktop_needs_full_blit = 1;
}

static void on_drag(int id, app_mouse_t *ev) {
    if (id < 0 || id >= MAX_WIN || !ev) return;
    if (!st[id].sb_dragging) return;

    int body_h   = ev->ch - TOOLBAR_H - 3 - STATUSBAR_H;
    int sh       = body_h;
    int visible, max_sc, thumb_h_dummy;
    sb_geometry(id, ev->cy, ev->cw, ev->ch,
                0, 0, 0, 0, &thumb_h_dummy, &visible, &max_sc);

    int track_h = sh - 22 - thumb_h_dummy;
    if (track_h < 1) track_h = 1;
    if (max_sc < 1) { st[id].sb_dragging = 0; return; }

    int delta      = ev->my - st[id].sb_drag_start_y;
    int new_scroll = st[id].sb_drag_start_scroll + (delta * max_sc) / track_h;
    if (new_scroll < 0)       new_scroll = 0;
    if (new_scroll > max_sc)  new_scroll = max_sc;
    if (st[id].scroll_y != new_scroll) {
        st[id].scroll_y = new_scroll;
        desktop_needs_full_blit = 1;
    }
}

static void on_release(int id, app_mouse_t *ev) {
    (void)ev;
    if (id >= 0 && id < MAX_WIN) st[id].sb_dragging = 0;
}

static void on_mouse_move(int id, app_mouse_t *ev) {
    if (id < 0 || id >= MAX_WIN || !ev) return;
    int pad      = 3;
    int body_top = TOOLBAR_H + pad;
    int body_h   = ev->ch - TOOLBAR_H - pad - STATUSBAR_H;
    int sx_rel   = ev->cw - SCROLLBAR_W;

    int rx = ev->mx - ev->cx;
    int ry = ev->my - ev->cy;
    int prev_hov = st[id].hovered_link;

    st[id].hovered_link = -1;
    if (ry >= body_top && ry < body_top + body_h && rx < sx_rel) {
        int row = (ry - body_top) / FONT_H + st[id].scroll_y;
        int col = (rx - pad - 1) / FONT_W;
        int lk;
        for (lk = 0; lk < st[id].link_count; lk++) {
            if (st[id].links[lk].line == row &&
                col >= st[id].links[lk].col &&
                col <  st[id].links[lk].col + st[id].links[lk].len)
            {
                st[id].hovered_link = lk;
                
                char msg[128];
                ksnprintf(msg, (int)sizeof(msg), "%s", st[id].links[lk].href);
                brow_set_status(id, msg);
                break;
            }
        }
        if (st[id].hovered_link < 0) {
            
            char tmp[64];
            ksnprintf(tmp, (int)sizeof(tmp), "%s%s", st[id].host, st[id].path);
            brow_set_status(id, tmp);
        }
    }
    if (st[id].hovered_link != prev_hov)
        desktop_needs_full_blit = 1;
}

extern const MenuItem menu_sav_items[];

static const MenuItem browser_file_items[] = {
    { "Home",    APP_NONE },
    { "Back",    APP_NONE },
    { "Forward", APP_NONE },
    { "Reload",  APP_NONE },
};

static Menu browser_menus[] = {
    { "@",    0, 0, menu_sav_items,      SV_MENU_SAV_COUNT },
    { "File", 0, 0, browser_file_items,  4 },
};

static int on_menu_action(int id, const char *label) {
    if (id < 0 || id >= MAX_WIN) return 0;
    if (str_eq(label, "Home"))    { brow_navigate(id, HOME_ADDR, 1); return 1; }
    if (str_eq(label, "Back"))    { brow_back(id);                   return 1; }
    if (str_eq(label, "Forward")) { brow_forward(id);                return 1; }
    if (str_eq(label, "Reload"))  { brow_navigate(id, st[id].addr, 0); return 1; }
    return 0;
}

const app_desc_t app_browser_desc = {
    .kind           = APP_browser,
    .default_title  = "Browser",
    .def_x          = 20,
    .def_y          = 16,
    .def_w          = 260,
    .def_h          = 180,
    .icon_bmp       = icon,
    .on_open        = on_open,
    .draw           = draw,
    .on_key         = on_key,
    .on_click       = on_click,
    .on_drag        = on_drag,
    .on_release     = on_release,
    
    .menu_bar_menus = browser_menus,
    .menu_bar_count = 2,
    .on_menu_action = on_menu_action,
};
