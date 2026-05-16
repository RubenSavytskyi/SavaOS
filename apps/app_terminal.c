

#include "sv_desktop_internal.h"
#include "sv_gfx.h"
#include "string.h"
#include "fs.h"
#include "fat32.h"
#include "ata.h"
#include "timer.h"
#include "rtc.h"
extern int ui_caret_blink_fast;

#define TERM_MAX_LINES        200
#define TERM_LINE_LEN         80
#define TERM_CLIPBOARD_SIZE   2048
#define TERM_CTX_MENU_ITEMS   4

#define SCROLLBAR_W           11
#define FONT_W                6
#define FONT_H                8
#define TERM_PADDING          4
#define INPUT_AREA_H          12

extern int ui_show_scrollbar_grid; 

typedef struct {
    char lines[TERM_MAX_LINES][TERM_LINE_LEN];
    int  line_count;
    char input[TERM_LINE_LEN];
    int  input_len;
    int  scroll_y;
} term_state_t;

static term_state_t term_states[MAX_WIN];

static char term_clipboard[TERM_CLIPBOARD_SIZE];
static int  term_clipboard_len = 0;

static int term_scrollbar_dragging       = -1;
static int term_scrollbar_drag_start_y   = 0;
static int term_scrollbar_drag_start_scroll = 0;

static int term_ctx_open  = 0;
static int term_ctx_x     = 0;
static int term_ctx_y     = 0;
static int term_ctx_hover = -1;
static int term_ctx_win   = -1;

static const char *ctx_labels[TERM_CTX_MENU_ITEMS] = {
    "Copy", "Paste", "Clear", "Select All"
};

extern int  str_len(const char *s);
extern int  str_eq(const char *a, const char *b);
extern int  str_starts_with(const char *s, const char *pfx);
extern int  ksnprintf(char *buf, int cap, const char *fmt, ...);
extern void str_cpy(char *d, const char *s, int max);

extern u8   inb(u16 port);
#define ATA_PRIMARY_STATUS 0x1F7

static void t_cpy(char *d, const char *s, int max) {
    int i;
    for (i = 0; i < max - 1 && s[i]; i++) d[i] = s[i];
    d[i] = 0;
}

static void term_push_line(int id, const char *s) {
    int i;
    if (id < 0 || id >= MAX_WIN) return;
    
    if (term_states[id].line_count >= TERM_MAX_LINES) {
        for (i = 1; i < TERM_MAX_LINES; i++) {
            t_cpy(term_states[id].lines[i-1], term_states[id].lines[i], TERM_LINE_LEN);
        }
        term_states[id].line_count = TERM_MAX_LINES - 1;
    }
    t_cpy(term_states[id].lines[term_states[id].line_count++], s, TERM_LINE_LEN);
}

static int term_total_lines(int id, int chars_per_line) {
    int total = 0;
    if (chars_per_line < 1) chars_per_line = 1;
    for (int i = 0; i < term_states[id].line_count; i++) {
        int ll = str_len(term_states[id].lines[i]);
        int w  = (ll + chars_per_line - 1) / chars_per_line;
        total += (w < 1) ? 1 : w;
    }
    return (total < 1) ? 1 : total;
}

static void term_scroll_to_bottom(int id, int visible_lines, int chars_per_line) {
    int total = term_total_lines(id, chars_per_line);
    int max_scroll = total - visible_lines;
    term_states[id].scroll_y = (max_scroll < 0) ? 0 : max_scroll;
}

static int chars_per_line_from_cw(int cw) {
    int content_w = cw - SCROLLBAR_W - 2;
    int c = (content_w - TERM_PADDING * 2) / FONT_W;
    return (c < 1) ? 1 : c;
}

static void action_copy(int id) {
    term_clipboard_len = 0;
    for (int i = 0; i < term_states[id].line_count; i++) {
        const char *ln = term_states[id].lines[i];
        int ll = str_len(ln);
        if (term_clipboard_len + ll + 1 >= TERM_CLIPBOARD_SIZE) break;
        for (int j = 0; j < ll; j++) term_clipboard[term_clipboard_len++] = ln[j];
        term_clipboard[term_clipboard_len++] = '\n';
    }
    if (term_clipboard_len > 0) term_clipboard_len--;
    term_clipboard[term_clipboard_len] = 0;
}

static void action_paste(int id) {
    int room = (TERM_LINE_LEN - 1) - term_states[id].input_len;
    int n = (term_clipboard_len < room) ? term_clipboard_len : room;
    for (int i = 0; i < n; i++) {
        char c = term_clipboard[i];
        if (c == '\n') break; 
        term_states[id].input[term_states[id].input_len++] = c;
    }
    term_states[id].input[term_states[id].input_len] = 0;
    desktop_needs_full_blit = 1;
}

static void action_clear(int id) {
    term_states[id].line_count = 0;
    term_states[id].scroll_y   = 0;
    desktop_needs_full_blit    = 1;
}

static void action_select_all(int id) {
    action_copy(id);
    term_states[id].scroll_y = 0;
    desktop_needs_full_blit = 1;
}

typedef void (*cmd_handler_t)(int id, const char *arg);
typedef struct { const char *name; cmd_handler_t handler; } term_cmd_t;

static void cmd_help(int id, const char *arg) {
    term_push_line(id, "Commands: help, clear, echo, ver, uname, time, about");
    term_push_line(id, "Files:    ls, cat, touch, rm");
    term_push_line(id, "FAT32:    fatls, fatmkdir, fatrm, fatrmdir");
    term_push_line(id, "Disk:     diskinfo, diskdir2, fsstate");
}
static void cmd_clear(int id, const char *arg) { action_clear(id); }
static void cmd_echo(int id, const char *arg)  { term_push_line(id, arg); }
static void cmd_ver(int id, const char *arg)   { term_push_line(id, "SavaOS 0.3"); }
static void cmd_uname(int id, const char *arg) { term_push_line(id, "SavaOS i386"); }
static void cmd_about(int id, const char *arg) { term_push_line(id, "SavaOS bare-metal GUI OS"); }

static void cmd_time(int id, const char *arg) {
    rtc_time_t t; char line[TERM_LINE_LEN]; rtc_read(&t);
    ksnprintf(line, sizeof(line), "%02d:%02d:%02d", (int)t.hour, (int)t.minute, (int)t.second);
    term_push_line(id, line);
}

static void cmd_ls(int id, const char *arg) {
    char names[FS_MAX_FILES][FS_MAX_NAME];
    int n = fs_list(names, FS_MAX_FILES);
    if (n == 0) term_push_line(id, "(empty)");
    for (int i = 0; i < n; i++) term_push_line(id, names[i]);
}

static void cmd_cat(int id, const char *arg) {
    char line[TERM_LINE_LEN];
    int fd = fs_open(arg);
    if (fd < 0) { term_push_line(id, "file not found"); return; }
    
    const char *data = fs_get_data(fd);
    int p = 0;
    while (data[p] && term_states[id].line_count < TERM_MAX_LINES - 1) {
        int k = 0;
        while (data[p] && data[p] != '\n' && data[p] != '\r' && k < TERM_LINE_LEN-1)
            line[k++] = data[p++];
        line[k] = 0;
        if (k > 0) term_push_line(id, line);
        while (data[p] == '\n' || data[p] == '\r') p++;
    }
}

static void cmd_touch(int id, const char *arg) {
    if (!arg[0]) { term_push_line(id, "usage: touch NAME"); return; }
    term_push_line(id, fs_create(arg) >= 0 ? "ok" : "error");
}

static void cmd_rm(int id, const char *arg) {
    term_push_line(id, fs_delete(arg) == 0 ? "deleted" : "not found");
}

static void cmd_fatls(int id, const char *arg) {
    if (!fs_using_fat32()) { term_push_line(id, "fat32 not mounted"); return; }
    FSDirEnt ents[FS_MAX_FILES];
    int n = fs_list_dir(fs_root_dir_cluster(), ents, FS_MAX_FILES);
    term_push_line(id, "/:");
    for (int i = 0; i < n; i++) {
        char out[TERM_LINE_LEN];
        if (ents[i].is_dir) ksnprintf(out, sizeof(out), "  %s/", ents[i].name);
        else ksnprintf(out, sizeof(out), "  %-12s %u", ents[i].name, (unsigned)ents[i].size);
        term_push_line(id, out);
    }
}

static void execute_fat_cmd(int id, const char *arg, const char *usage, int (*func)(u32, const char*), const char *success) {
    if (!fs_using_fat32()) { term_push_line(id, "fat32 not mounted"); return; }
    if (!arg[0]) { term_push_line(id, usage); return; }
    term_push_line(id, func(fs_root_dir_cluster(), arg) == 0 ? success : "fail");
}

static void cmd_fatmkdir(int id, const char *arg) { execute_fat_cmd(id, arg, "usage: fatmkdir NAME", fs_mkdir, "ok"); }
static void cmd_fatrm(int id, const char *arg)    { execute_fat_cmd(id, arg, "usage: fatrm NAME", fs_unlink, "deleted"); }
static void cmd_fatrmdir(int id, const char *arg) { execute_fat_cmd(id, arg, "usage: fatrmdir NAME", fs_rmdir_empty, "removed"); }

static const term_cmd_t term_commands[] = {
    {"help", cmd_help}, {"clear", cmd_clear}, {"echo", cmd_echo},
    {"ver", cmd_ver}, {"uname", cmd_uname}, {"about", cmd_about}, {"time", cmd_time},
    {"ls", cmd_ls}, {"cat", cmd_cat}, {"touch", cmd_touch}, {"rm", cmd_rm},
    {"fatls", cmd_fatls}, {"fatmkdir", cmd_fatmkdir}, {"fatrm", cmd_fatrm}, {"fatrmdir", cmd_fatrmdir},
    {NULL, NULL}
};

static void term_exec_command(int id, const char *cmd) {
    if (cmd[0] == 0) return;
    int i = 0;
    while (term_commands[i].name) {
        int len = str_len(term_commands[i].name);
        if (str_starts_with(cmd, term_commands[i].name)) {
            if (cmd[len] == 0) { term_commands[i].handler(id, ""); return; }
            else if (cmd[len] == ' ') { term_commands[i].handler(id, cmd + len + 1); return; }
        }
        i++;
    }
    char line[TERM_LINE_LEN];
    if (str_eq(cmd, "fsstate")) {
        char names[FS_MAX_FILES][FS_MAX_NAME];
        ksnprintf(line, sizeof(line), "fat32=%d files=%d", fs_using_fat32(), fs_list(names, FS_MAX_FILES));
        term_push_line(id, line);
        return;
    }
    ksnprintf(line, sizeof(line), "'%s': unknown command", cmd);
    term_push_line(id, line);
}

static void on_key(int id, int key) {
    if (id < 0 || id >= MAX_WIN) return;

    if (key == 0x08) {
        if (term_states[id].input_len > 0) {
            term_states[id].input_len--;
            term_states[id].input[term_states[id].input_len] = 0;
            desktop_needs_full_blit = 1;
        }
        return;
    }
    if (key == 0x0D) {
        char cmdline[TERM_LINE_LEN + 4];
        ksnprintf(cmdline, sizeof(cmdline), "> %s", term_states[id].input);
        term_push_line(id, cmdline);
        term_exec_command(id, term_states[id].input);
        term_states[id].input_len = 0; term_states[id].input[0] = 0;
        
        int text_area_h = wins[id].h - SVS_TITLE_H - 3 - 4 - INPUT_AREA_H;
        int vis = text_area_h / FONT_H;
        term_scroll_to_bottom(id, (vis < 1) ? 1 : vis, chars_per_line_from_cw(wins[id].w - 6));
        desktop_needs_full_blit = 1;
        return;
    }
    if (key >= 32 && key <= 126) {
        if (term_states[id].input_len < TERM_LINE_LEN - 1) {
            term_states[id].input[term_states[id].input_len++] = (char)key;
            term_states[id].input[term_states[id].input_len]   = 0;
            desktop_needs_full_blit = 1;
        }
        return;
    }
    if (key == 0x48 && term_states[id].scroll_y > 0) { term_states[id].scroll_y--; desktop_needs_full_blit = 1; }
    if (key == 0x50) { term_states[id].scroll_y++; desktop_needs_full_blit = 1; }
}

static const char icon[24][25] = {
    "........................",
    "........................",
    "...WWWWWWWWWWWWWWWWWW...",
    "..WBBBBBBBBBBBBBBBBBBW..",
    "..WBBBBBBBBBBBBBBBBBBW..",
    "..WBBWBBBBBBBBBBBBBBBW..",
    "..WBBBWBBBBBBBBBBBBBBW..",
    "..WBBBBWBBBBBBBBBBBBBW..",
    "..WBBBWBBBBBBBBBBBBBBW..",
    "..WBBWBBBWWWBBBBBBBBBW..",
    "..WBBBBBBBBBBBBBBBBBBW..",
    "..WBBBBBBBBBBBBBBBBBBW..",
    "..WBBBBBBBBBBBBBBBBBBW..",
    "..WBBBBBBBBBBBBBBBBBBW..",
    "..WBBBBBBBBBBBBBBBBBBW..",
    "..WBBBBBBBBBBBBBBBBBBW..",
    "..WBBBBBBBBBBBBBBBBBBW..",
    "..WBBBBBBBBBBBBBBBBBBW..",
    "..WBBBBBBBBBBBBBBBBBBW..",
    "..WBBBBBBBBBBBBBBBBBBW..",
    "..WBBBBBBBBBBBBBBBBBBW..",
    "...WWWWWWWWWWWWWWWWWW...",
    "........................",
    "........................"
};

static void draw(int id, int cx, int cy, int cw, int ch) {
    if (id < 0 || id >= MAX_WIN) return;

    int content_w     = cw - SCROLLBAR_W - 2;
    int content_h     = ch - 4;
    int text_area_h   = content_h - INPUT_AREA_H;
    int visible_lines = text_area_h / FONT_H;
    if (visible_lines < 1) visible_lines = 1;

    int cpl           = chars_per_line_from_cw(cw);
    int total_lines   = term_total_lines(id, cpl);

    
    int max_scroll = total_lines - visible_lines;
    if (max_scroll < 0) max_scroll = 0;
    if (term_states[id].scroll_y > max_scroll) term_states[id].scroll_y = max_scroll;
    if (term_states[id].scroll_y < 0)          term_states[id].scroll_y = 0;

    

    vga13_fill_rect(cx, cy, content_w + 1, ch, VGA13_BLACK);

    
    int draw_y  = cy + TERM_PADDING;
    int skip    = term_states[id].scroll_y;
    int skipped = 0;

    for (int li = 0; li < term_states[id].line_count && draw_y + FONT_H - 1 < cy + ch - INPUT_AREA_H; li++) {
        const char *ln = term_states[id].lines[li];
        int ll = str_len(ln);
        int num_wraps = (ll + cpl - 1) / cpl;
        if (num_wraps < 1) num_wraps = 1;

        for (int wi = 0; wi < num_wraps && draw_y + FONT_H - 1 < cy + ch - INPUT_AREA_H; wi++) {
            if (skipped < skip) { skipped++; continue; }
            
            char seg[TERM_LINE_LEN];
            int seg_start = wi * cpl;
            int seg_len = ll - seg_start;
            if (seg_len > cpl) seg_len = cpl;
            if (seg_len > TERM_LINE_LEN - 1) seg_len = TERM_LINE_LEN - 1;
            
            if (seg_len > 0) {
                for (int k = 0; k < seg_len; k++) seg[k] = ln[seg_start + k];
                seg[seg_len] = 0;
                vga13_draw_string(cx + TERM_PADDING, draw_y, seg, VGA13_WHITE, VGA13_BLACK, 0);
            }
            draw_y += FONT_H;
        }
    }

    
    int input_y = cy + ch - INPUT_AREA_H;
    vga13_fill_rect(cx, input_y - 1, content_w + 1, INPUT_AREA_H - 1, VGA13_BLACK);
 
    char prompt[TERM_LINE_LEN + 4];
    ksnprintf(prompt, sizeof(prompt), "> %s", term_states[id].input);
    vga13_draw_string(cx + TERM_PADDING, input_y + 1, prompt, VGA13_WHITE, VGA13_BLACK, 0);

    int cur_x = cx + TERM_PADDING + (2 + term_states[id].input_len) * FONT_W;
{
    u32 phase = (u32)(ui_caret_blink_fast ? 40u : 120u);
    if (((u32)timer_ticks() / phase) & 1u)
        vline_px(cur_x, input_y + 1, input_y + FONT_H, VGA13_WHITE);
}
    

    int scroll_x = cx + cw - SCROLLBAR_W - 1;
    int scroll_y = cy + 2;
    int scroll_h = content_h;
    int thumb_h, thumb_y;

    if (total_lines <= visible_lines) {
        thumb_h = scroll_h - 26;
        thumb_y = scroll_y + 13;
    } else {
        thumb_h = (visible_lines * (scroll_h - 26)) / total_lines;
        if (thumb_h < 20) thumb_h = 20;
        thumb_y = scroll_y + 13 + (term_states[id].scroll_y * (scroll_h - 26 - thumb_h)) / (total_lines - visible_lines);
    }

    vga13_fill_rect(scroll_x, scroll_y, SCROLLBAR_W, scroll_h, PAL_LIGHT_GRAY);

    if (ui_show_scrollbar_grid) {
        for (int gx = scroll_x + 2; gx <= scroll_x + SCROLLBAR_W - 3; gx += 2) {
            vline_px(gx, scroll_y + 2, scroll_y + scroll_h - 3, PAL_DARK_GRAY);
        }
        for (int gy = scroll_y + 2; gy <= scroll_y + scroll_h - 3; gy += 4) {
            hline_px(scroll_x + 2, scroll_x + SCROLLBAR_W - 3, gy, PAL_DARK_GRAY);
        }
    }

    hline_px(scroll_x, scroll_x + SCROLLBAR_W - 1, scroll_y, VGA13_BLACK);
    hline_px(scroll_x, scroll_x + SCROLLBAR_W - 1, scroll_y + scroll_h - 1, VGA13_BLACK);
    vline_px(scroll_x, scroll_y, scroll_y + scroll_h - 1, VGA13_BLACK);
    vline_px(scroll_x + SCROLLBAR_W - 1, scroll_y, scroll_y + scroll_h - 1, VGA13_BLACK);

    
    vga13_fill_rect(scroll_x + 1, scroll_y + 1, SCROLLBAR_W - 2, 10, VGA13_WHITE);
    hline_px(scroll_x + 1, scroll_x + SCROLLBAR_W - 2, scroll_y + 1, VGA13_BLACK);
    hline_px(scroll_x + 1, scroll_x + SCROLLBAR_W - 2, scroll_y + 10, VGA13_BLACK);
    vline_px(scroll_x + 1, scroll_y + 1, scroll_y + 10, VGA13_BLACK);
    vline_px(scroll_x + SCROLLBAR_W - 2, scroll_y + 1, scroll_y + 10, VGA13_BLACK);
    vga13_draw_string(scroll_x + 3, scroll_y + 2, "\x1E", VGA13_BLACK, VGA13_WHITE, 0);

    
    vga13_fill_rect(scroll_x + 1, scroll_y + scroll_h - 11, SCROLLBAR_W - 2, 10, VGA13_WHITE);
    hline_px(scroll_x + 1, scroll_x + SCROLLBAR_W - 2, scroll_y + scroll_h - 11, VGA13_BLACK);
    hline_px(scroll_x + 1, scroll_x + SCROLLBAR_W - 2, scroll_y + scroll_h - 1, VGA13_BLACK);
    vline_px(scroll_x + 1, scroll_y + scroll_h - 11, scroll_y + scroll_h - 1, VGA13_BLACK);
    vline_px(scroll_x + SCROLLBAR_W - 2, scroll_y + scroll_h - 11, scroll_y + scroll_h - 1, VGA13_BLACK);
    vga13_draw_string(scroll_x + 3, scroll_y + scroll_h - 10, "\x1F", VGA13_BLACK, VGA13_WHITE, 0);

    
    vga13_fill_rect(scroll_x + 2, thumb_y, SCROLLBAR_W - 4, thumb_h, PAL_LIGHT_GRAY);
    hline_px(scroll_x + 2, scroll_x + SCROLLBAR_W - 3, thumb_y, VGA13_BLACK);
    hline_px(scroll_x + 2, scroll_x + SCROLLBAR_W - 3, thumb_y + thumb_h - 1, VGA13_BLACK);
    vline_px(scroll_x + 2, thumb_y, thumb_y + thumb_h - 1, VGA13_BLACK);
    vline_px(scroll_x + SCROLLBAR_W - 3, thumb_y, thumb_y + thumb_h - 1, VGA13_BLACK);
    hline_px(scroll_x + 3, scroll_x + SCROLLBAR_W - 4, thumb_y + 1, VGA13_WHITE);
    vline_px(scroll_x + 3, thumb_y + 1, thumb_y + thumb_h - 2, VGA13_WHITE);
}

static int ctx_width(void) {
    int w = 70;
    for (int i = 0; i < TERM_CTX_MENU_ITEMS; i++) {
        int tw = str_len(ctx_labels[i]) * FONT_W + 16;
        if (tw > w) w = tw;
    }
    return w;
}

static void draw_overlay(void) {
    if (!term_ctx_open) return;
    int w = ctx_width(), h = TERM_CTX_MENU_ITEMS * 11 + 6;
    if (term_ctx_x + w > VGA13_WIDTH)  term_ctx_x = VGA13_WIDTH  - w;
    if (term_ctx_y + h > VGA13_HEIGHT) term_ctx_y = VGA13_HEIGHT - h;

    vga13_fill_rect(term_ctx_x + 2, term_ctx_y + h, w, 2, PAL_DARK_GRAY);
    vga13_fill_rect(term_ctx_x + w, term_ctx_y + 2, 2, h, PAL_DARK_GRAY);
    
    vga13_fill_rect(term_ctx_x, term_ctx_y, w, h, VGA13_WHITE);
    hline_px(term_ctx_x, term_ctx_x + w - 1, term_ctx_y, VGA13_BLACK);
    hline_px(term_ctx_x, term_ctx_x + w - 1, term_ctx_y + h - 1, VGA13_BLACK);
    vline_px(term_ctx_x, term_ctx_y, term_ctx_y + h - 1, VGA13_BLACK);
    vline_px(term_ctx_x + w - 1, term_ctx_y, term_ctx_y + h - 1, VGA13_BLACK);

    for (int i = 0; i < TERM_CTX_MENU_ITEMS; i++) {
        int iy  = term_ctx_y + 2 + i * 11;
        int inv = (i == term_ctx_hover);
        if (inv) vga13_fill_rect(term_ctx_x + 2, iy - 1, w - 4, 10, VGA13_BLACK);
        vga13_draw_string(term_ctx_x + 8, iy, ctx_labels[i], VGA13_BLACK, VGA13_WHITE, inv);
    }
}

static int hit_overlay(int mx, int my) {
    if (!term_ctx_open) return -1;
    int w = ctx_width(), h = TERM_CTX_MENU_ITEMS * 11 + 6;
    if (mx < term_ctx_x || mx >= term_ctx_x + w || my < term_ctx_y || my >= term_ctx_y + h) return -1;
    return (my - term_ctx_y - 3) / 11;
}

static void hover_overlay(int mx, int my) { term_ctx_hover = hit_overlay(mx, my); }
static int overlay_is_open(void) { return term_ctx_open; }
static void close_overlay(void) { term_ctx_open = 0; term_ctx_hover = -1; }

static void click_overlay(int mx, int my, int win_id) {
    int item = hit_overlay(mx, my);
    if (item >= 0 && win_id >= 0 && win_id < MAX_WIN) {
        if (item == 0) action_copy(win_id);
        else if (item == 1) action_paste(win_id);
        else if (item == 2) action_clear(win_id);
        else if (item == 3) action_select_all(win_id);
    }
    close_overlay();
    desktop_needs_full_blit = 1;
}

static void on_click(int id, app_mouse_t *ev) {
    if (ev->button == 2) { 
        term_ctx_x = ev->mx; term_ctx_y = ev->my;
        term_ctx_open = 1; term_ctx_hover = -1; term_ctx_win = id;
        desktop_needs_full_blit = 1;
        return;
    }

    int scroll_x = ev->cx + ev->cw - SCROLLBAR_W - 1;
    int scroll_y = ev->cy + 2;
    int scroll_h = ev->ch - 4;

    if (ev->mx < scroll_x || ev->mx >= scroll_x + SCROLLBAR_W ||
        ev->my < scroll_y || ev->my >= scroll_y + scroll_h) {
        return;
    }

    int cpl = chars_per_line_from_cw(ev->cw);
    int total_lines = term_total_lines(id, cpl);
    int text_area_h = ev->ch - 4 - INPUT_AREA_H;
    int visible_lines = text_area_h / FONT_H;
    if (visible_lines < 1) visible_lines = 1;

    if (ev->my >= scroll_y + 1 && ev->my <= scroll_y + 10) {
        if (term_states[id].scroll_y > 0) {
            term_states[id].scroll_y--;
            desktop_needs_full_blit = 1;
        }
        return;
    }

    if (ev->my >= scroll_y + scroll_h - 11 && ev->my <= scroll_y + scroll_h - 1) {
        if (term_states[id].scroll_y < total_lines - visible_lines) {
            term_states[id].scroll_y++;
            desktop_needs_full_blit = 1;
        }
        return;
    }

    int click_in_track = (ev->my > scroll_y + 10 && ev->my < scroll_y + scroll_h - 11);

    if (click_in_track && total_lines > visible_lines) {
        int thumb_h = (visible_lines * (scroll_h - 26)) / total_lines;
        if (thumb_h < 20) thumb_h = 20;
        int thumb_y = scroll_y + 13 + (term_states[id].scroll_y * (scroll_h - 26 - thumb_h)) / (total_lines - visible_lines);

        if (ev->my < thumb_y) {
            term_states[id].scroll_y -= visible_lines;
            if (term_states[id].scroll_y < 0) term_states[id].scroll_y = 0;
            desktop_needs_full_blit = 1;
        } else if (ev->my >= thumb_y + thumb_h) {
            term_states[id].scroll_y += visible_lines;
            if (term_states[id].scroll_y > total_lines - visible_lines)
                term_states[id].scroll_y = total_lines - visible_lines;
            desktop_needs_full_blit = 1;
        } else {
            term_scrollbar_dragging = id;
            term_scrollbar_drag_start_y = ev->my;
            term_scrollbar_drag_start_scroll = term_states[id].scroll_y;
        }
    }
}

static void on_drag(int id, app_mouse_t *ev) {
    if (term_scrollbar_dragging != id) return;

    int scroll_h = ev->ch - 4;
    int cpl = chars_per_line_from_cw(ev->cw);
    int total_lines = term_total_lines(id, cpl);
    int text_area_h = ev->ch - 4 - INPUT_AREA_H;
    int visible_lines = text_area_h / FONT_H;
    if (visible_lines < 1) visible_lines = 1;

    if (total_lines <= visible_lines) return;

    int thumb_h = (visible_lines * (scroll_h - 26)) / total_lines;
    if (thumb_h < 20) thumb_h = 20;

    int usable = scroll_h - 26 - thumb_h;
    if (usable < 1) return;

    int max_scroll = total_lines - visible_lines;
    int delta = ev->my - term_scrollbar_drag_start_y;
    int new_scroll = term_scrollbar_drag_start_scroll + (delta * max_scroll) / usable;

    if (new_scroll < 0) new_scroll = 0;
    if (new_scroll > max_scroll) new_scroll = max_scroll;

    if (term_states[id].scroll_y != new_scroll) {
        term_states[id].scroll_y = new_scroll;
        desktop_needs_full_blit = 1;
    }
}

static void on_release(int id, app_mouse_t *ev) {
    (void)id; (void)ev;
    term_scrollbar_dragging = -1;
}

static void on_right_click(int id, app_mouse_t *ev) {
    term_ctx_x = ev->mx; term_ctx_y = ev->my;
    term_ctx_open = 1; term_ctx_hover = -1; term_ctx_win = id;
    desktop_needs_full_blit = 1;
}

static void on_open(int id) {
    if (id < 0 || id >= MAX_WIN) return;
    term_states[id].line_count = 0;
    term_states[id].input_len  = 0;
    term_states[id].input[0]   = 0;
    term_states[id].scroll_y   = 0;
    term_push_line(id, "SavaOS Terminal");
    term_push_line(id, "Type 'help' for commands.");
}

void term_init(int max_windows) { (void)max_windows; }

extern const MenuItem menu_sav_items[];
static const MenuItem term_edit_items[] = {
    { "Copy",  APP_NONE },
    { "Paste", APP_NONE },
    { "Clear", APP_NONE },
    { "Select All", APP_NONE },
};
static Menu term_menus[] = {
    { "@",    0, 0, menu_sav_items, SV_MENU_SAV_COUNT },
    { "Edit", 0, 0, term_edit_items, 4 },
};

static int on_menu_action(int id, const char *label) {
    if (id < 0 || id >= MAX_WIN) return 0;
    if (str_eq(label, "Copy"))  { action_copy(id);  return 1; }
    if (str_eq(label, "Paste")) { action_paste(id); return 1; }
    if (str_eq(label, "Clear")) { action_clear(id); return 1; }
    if (str_eq(label, "Select All")) { action_select_all(id); return 1; }
    return 0;
}

const app_desc_t app_terminal_desc = {
    .kind          = APP_terminal,
    .default_title = "Terminal",
    .def_x = 50, .def_y = 35, .def_w = 220, .def_h = 150,

    .icon_bmp = icon,

    .on_open        = on_open,
    .draw           = draw,
    .on_key         = on_key,
    .on_click       = on_click,
    .on_drag        = on_drag,
    .on_release     = on_release,
    .on_right_click = on_right_click,

    .draw_overlay   = draw_overlay,
    .hit_overlay    = hit_overlay,
    .hover_overlay  = hover_overlay,
    .overlay_is_open= overlay_is_open,
    .close_overlay  = close_overlay,
    .click_overlay  = click_overlay,

    .menu_bar_menus = term_menus,
    .menu_bar_count = 2,
    .on_menu_action = on_menu_action,
};