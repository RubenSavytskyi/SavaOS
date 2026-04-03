#include "terminal.h"
#include "string.h"
#include "sv_gfx.h"
#include "fs.h"
#include "fat32.h"
#include "ata.h"
#include "timer.h"
#include "rtc.h"

#define MAX_WIN 8


static void term_str_cpy(char *d, const char *s, int max) {
    int i;
    for (i = 0; i < max - 1 && s[i]; i++) d[i] = s[i];
    d[i] = 0;
}


term_state_t term_states[MAX_WIN];
int term_max_win = 0;


char term_clipboard[TERM_CLIPBOARD_SIZE];
int term_clipboard_len = 0;


int term_scrollbar_dragging = -1;
int term_scrollbar_drag_start_y = 0;
int term_scrollbar_drag_start_scroll = 0;


int term_ctx_menu_open = 0;
int term_ctx_menu_x = 0;
int term_ctx_menu_y = 0;
int term_ctx_menu_hover = -1;
const char *term_ctx_menu_labels[TERM_CTX_MENU_ITEMS] = { "Cut", "Copy", "Paste", "Clear" };


extern int desktop_needs_full_blit;
extern int str_len(const char *s);
extern void str_cpy(char *d, const char *s, int max);
extern int str_eq(const char *a, const char *b);
extern int str_starts_with(const char *s, const char *pfx);
extern void str_copy_n(char *dst, const char *src, int max);
extern int ksnprintf(char *buf, int cap, const char *fmt, ...);


typedef enum {
    APP_NONE = 0,
    APP_ABOUT,
    APP_NOTEPAD,
    APP_CALC,
    APP_TERMINAL,
    APP_DISK,
    APP_TRASH,
    APP_CONTROL_PANEL,
    APP_PUZZLE
} app_kind_t;


extern app_kind_t win_get_app(int id);
extern int win_is_valid(int id);


extern u8 inb(u16 port);
#define ATA_PRIMARY_STATUS 0x1F7

void term_init(int max_windows) {
    term_max_win = max_windows;
    term_clipboard_len = 0;
    term_scrollbar_dragging = -1;
    term_ctx_menu_open = 0;
    term_ctx_menu_hover = -1;
}

void term_window_init(int id) {
    if (id < 0 || id >= term_max_win) return;
    term_states[id].line_count = 0;
    term_states[id].input_len = 0;
    term_states[id].input[0] = 0;
    term_states[id].scroll_y = 0;
    term_push_line(id, "System Terminal");
    term_push_line(id, "Type 'help' for commands.");
}

const char* term_get_line(int id, int line_idx) {
    if (id < 0 || id >= term_max_win) return "";
    if (line_idx < 0 || line_idx >= term_states[id].line_count) return "";
    return term_states[id].lines[line_idx];
}

int term_get_line_count(int id) {
    if (id < 0 || id >= term_max_win) return 0;
    return term_states[id].line_count;
}

int term_get_scroll_y(int id) {
    if (id < 0 || id >= term_max_win) return 0;
    return term_states[id].scroll_y;
}

const char* term_get_input(int id) {
    if (id < 0 || id >= term_max_win) return "";
    return term_states[id].input;
}

int term_get_input_len(int id) {
    if (id < 0 || id >= term_max_win) return 0;
    return term_states[id].input_len;
}

void term_push_line(int id, const char *s) {
    int i;
    if (id < 0 || id >= term_max_win) return;
    if (term_states[id].line_count >= TERM_MAX_LINES) {
        for (i = 1; i < TERM_MAX_LINES; i++)
            term_str_cpy(term_states[id].lines[i - 1], term_states[id].lines[i], TERM_LINE_LEN);
        term_states[id].line_count = TERM_MAX_LINES - 1;
    }
    term_str_cpy(term_states[id].lines[term_states[id].line_count++], s, TERM_LINE_LEN);
}

void term_clear(int id) {
    if (id < 0 || id >= term_max_win) return;
    term_states[id].line_count = 0;
}

void term_exec_command(int id, const char *cmd) {
    char line[TERM_LINE_LEN];
    char arg[TERM_LINE_LEN];
    int i;
    if (str_eq(cmd, "help")) {
        term_push_line(id, "help");
        term_push_line(id, "clear");
        term_push_line(id, "echo");
        term_push_line(id, "about");
        term_push_line(id, "time");
        term_push_line(id, "ls");
        term_push_line(id, "cat");
        term_push_line(id, "touch");
        term_push_line(id, "rm");
        term_push_line(id, "ver");
        term_push_line(id, "uname");
        term_push_line(id, "diskinfo");
        term_push_line(id, "diskdir2");
        term_push_line(id, "fsstate");
        term_push_line(id, "mounttest");
        term_push_line(id, "mounttest2");
        term_push_line(id, "atatest");
        term_push_line(id, "identifytest");
        term_push_line(id, "fatls");
        term_push_line(id, "fatmkdir");
        term_push_line(id, "fatrm");
        term_push_line(id, "fatrmdir");
    } else if (str_eq(cmd, "ls")) {
        char names[FS_MAX_FILES][FS_MAX_NAME];
        int n = fs_list(names, FS_MAX_FILES);
        term_push_line(id, "files:");
        for (i = 0; i < n; i++) term_push_line(id, names[i]);
    } else if (str_eq(cmd, "fatls")) {
        if (!fs_using_fat32()) { term_push_line(id, "fatls: fat32 not mounted"); return; }
        FSDirEnt ents[FS_MAX_FILES];
        int n = fs_list_dir(fs_root_dir_cluster(), ents, FS_MAX_FILES);
        term_push_line(id, "/:");
        for (i = 0; i < n && term_states[id].line_count < TERM_MAX_LINES - 1; i++) {
            char out[TERM_LINE_LEN];
            if (ents[i].is_dir) {
                ksnprintf(out, sizeof(out), "%s/", ents[i].name);
            } else {
                ksnprintf(out, sizeof(out), "%s (%u)", ents[i].name, (unsigned)ents[i].size);
            }
            term_push_line(id, out);
        }
    } else if (str_starts_with(cmd, "fatmkdir ")) {
        if (!fs_using_fat32()) { term_push_line(id, "fatmkdir: fat32 not mounted"); return; }
        str_copy_n(arg, cmd + 9, TERM_LINE_LEN);
        if (!arg[0]) { term_push_line(id, "usage: fatmkdir NAME"); return; }
        int r = fs_mkdir(fs_root_dir_cluster(), arg);
        term_push_line(id, (r == 0) ? "ok" : "fail");
    } else if (str_starts_with(cmd, "fatrm ")) {
        if (!fs_using_fat32()) { term_push_line(id, "fatrm: fat32 not mounted"); return; }
        str_copy_n(arg, cmd + 6, TERM_LINE_LEN);
        if (!arg[0]) { term_push_line(id, "usage: fatrm NAME"); return; }
        int r = fs_unlink(fs_root_dir_cluster(), arg);
        term_push_line(id, (r == 0) ? "deleted" : "fail");
    } else if (str_starts_with(cmd, "fatrmdir ")) {
        if (!fs_using_fat32()) { term_push_line(id, "fatrmdir: fat32 not mounted"); return; }
        str_copy_n(arg, cmd + 9, TERM_LINE_LEN);
        if (!arg[0]) { term_push_line(id, "usage: fatrmdir NAME"); return; }
        int r = fs_rmdir_empty(fs_root_dir_cluster(), arg);
        term_push_line(id, (r == 0) ? "removed" : "fail");
    } else if (str_starts_with(cmd, "cat ")) {
        int fd;
        str_copy_n(arg, cmd + 4, TERM_LINE_LEN);
        fd = fs_open(arg);
        if (fd < 0) term_push_line(id, "file not found");
        else {
            const char *data = fs_get_data(fd);
            int p = 0;
            while (data[p]) {
                int k = 0;
                while (data[p] && data[p] != '\n' && data[p] != '\r' && k < TERM_LINE_LEN - 1)
                    line[k++] = data[p++];
                line[k] = 0;
                if (k > 0) term_push_line(id, line);
                while (data[p] == '\n' || data[p] == '\r') p++;
                if (term_states[id].line_count >= TERM_MAX_LINES - 1) break;
            }
        }
    } else if (str_starts_with(cmd, "touch ")) {
        str_copy_n(arg, cmd + 6, TERM_LINE_LEN);
        if (arg[0]) {
            int r = fs_create(arg);
            if (r >= 0) term_push_line(id, "ok");
            else term_push_line(id, "read-only fs");
        }
    } else if (str_starts_with(cmd, "rm ")) {
        str_copy_n(arg, cmd + 3, TERM_LINE_LEN);
        int r = fs_delete(arg);
        if (r == 0) term_push_line(id, "deleted");
        else if (fs_using_fat32()) term_push_line(id, "read-only fs");
        else term_push_line(id, "file not found");
    } else if (str_eq(cmd, "ver")) {
        term_push_line(id, "SavaOS 0.1");
    } else if (str_eq(cmd, "uname")) {
        term_push_line(id, "SavaOS i386");
    } else if (str_eq(cmd, "clear")) {
        term_states[id].line_count = 0;
    } else if (str_starts_with(cmd, "echo ")) {
        term_push_line(id, cmd + 5);
    } else if (str_eq(cmd, "diskinfo")) {
        char dbg[512];
        dbg[0] = 0;
        fat32_debug_bpb(dbg, (int)sizeof(dbg));
        int p = 0;
        while (dbg[p] && term_states[id].line_count < TERM_MAX_LINES - 1) {
            int k = 0;
            while (dbg[p] && dbg[p] != '\n' && dbg[p] != '\r' && k < TERM_LINE_LEN - 1) {
                line[k++] = dbg[p++];
            }
            line[k] = 0;
            if (k > 0) term_push_line(id, line);
            while (dbg[p] == '\n' || dbg[p] == '\r') p++;
        }
        if (term_states[id].line_count == 0) term_push_line(id, "diskinfo: no output");
    } else if (str_eq(cmd, "diskdir2")) {
        char dbg[512];
        dbg[0] = 0;
        fat32_debug_cluster2(dbg, (int)sizeof(dbg));
        int p = 0;
        while (dbg[p] && term_states[id].line_count < TERM_MAX_LINES - 1) {
            int k = 0;
            while (dbg[p] && dbg[p] != '\n' && dbg[p] != '\r' && k < TERM_LINE_LEN - 1) {
                line[k++] = dbg[p++];
            }
            line[k] = 0;
            if (k > 0) term_push_line(id, line);
            while (dbg[p] == '\n' || dbg[p] == '\r') p++;
        }
        if (term_states[id].line_count == 0) term_push_line(id, "diskdir2: no output");
    } else if (str_eq(cmd, "fsstate")) {
        char dbg[128];
        char names[FS_MAX_FILES][FS_MAX_NAME];
        int using = fs_using_fat32();
        int n = fs_list(names, FS_MAX_FILES);
        ksnprintf(dbg, sizeof(dbg), "use_fat32=%d fs_list_n=%d", using, n);
        term_push_line(id, dbg);
        if (n > 0) term_push_line(id, names[0]);
        else term_push_line(id, "<none>");
    } else if (str_eq(cmd, "mounttest")) {
        char dbg[512];
        dbg[0] = 0;
        fat32_unmount();
        int ret = fat32_mount();
        char out[64];
        ksnprintf(out, sizeof(out), "fat32_mount ret=%d", ret);
        term_push_line(id, out);
        fat32_debug_bpb(dbg, (int)sizeof(dbg));
        int p = 0;
        while (dbg[p] && term_states[id].line_count < TERM_MAX_LINES - 1) {
            int k = 0;
            while (dbg[p] && dbg[p] != '\n' && dbg[p] != '\r' && k < TERM_LINE_LEN - 1) {
                line[k++] = dbg[p++];
            }
            line[k] = 0;
            if (k > 0) term_push_line(id, line);
            while (dbg[p] == '\n' || dbg[p] == '\r') p++;
        }
        if (term_states[id].line_count == 0) term_push_line(id, "mounttest: no output");
    } else if (str_eq(cmd, "mounttest2")) {
        char dbg[512];
        dbg[0] = 0;
        ata_init();
        fat32_unmount();
        int ret = fat32_mount();
        char out[64];
        ksnprintf(out, sizeof(out), "fat32_mount ret=%d", ret);
        term_push_line(id, out);
        int p = 0;
        while (dbg[p] && term_states[id].line_count < TERM_MAX_LINES - 1) {
            int k = 0;
            while (dbg[p] && dbg[p] != '\n' && dbg[p] != '\r' && k < TERM_LINE_LEN - 1) {
                line[k++] = dbg[p++];
            }
            line[k] = 0;
            if (k > 0) term_push_line(id, line);
            while (dbg[p] == '\n' || dbg[p] == '\r') p++;
        }
        if (term_states[id].line_count == 0) term_push_line(id, "mounttest2: no output");
    } else if (str_eq(cmd, "atatest")) {
        u8 buf[512];
        ata_init();
        u8 st_before = inb(ATA_PRIMARY_STATUS);
        int ret = ata_read_sectors(0, 1, buf);
        u8 st_after = inb(ATA_PRIMARY_STATUS);
        char out[128];
        ksnprintf(out, sizeof(out),
                  "ata_read_sectors ret=%d status_before=%02x status_after=%02x",
                  ret, (unsigned)st_before, (unsigned)st_after);
        term_push_line(id, out);
        if (ret == 0) {
            u8 b510 = buf[510];
            u8 b511 = buf[511];
            char fs[9];
            for (int i = 0; i < 8; i++) fs[i] = (char)buf[82 + i];
            fs[8] = 0;
            char sigline[64];
            ksnprintf(sigline, sizeof(sigline), "sig bytes=%02x %02x fs=%s", b510, b511, fs);
            term_push_line(id, sigline);
        }
    } else if (str_eq(cmd, "identifytest")) {
        ata_init();
        int ret = ata_identify();
        char out[64];
        ksnprintf(out, sizeof(out), "ata_identify ret=%d", ret);
        term_push_line(id, out);
    } else if (str_eq(cmd, "about")) {
        term_push_line(id, "SavaOS bare-metal GUI demo");
    } else if (str_eq(cmd, "time")) {
        rtc_time_t t;
        rtc_read(&t);
        line[0] = '0' + (char)(t.hour / 10); line[1] = '0' + (char)(t.hour % 10);
        line[2] = ':';
        line[3] = '0' + (char)(t.minute / 10); line[4] = '0' + (char)(t.minute % 10);
        line[5] = ':';
        line[6] = '0' + (char)(t.second / 10); line[7] = '0' + (char)(t.second % 10);
        line[8] = 0;
        term_push_line(id, line);
    } else if (cmd[0] != 0) {
        term_push_line(id, "Unknown command. Type 'help'.");
    }
}

void handle_terminal_key(int id, int key) {
    char cmdline[TERM_LINE_LEN + 4];
    if (id < 0 || id >= term_max_win) return;
    if (key == 0x08) { 
        if (term_states[id].input_len > 0) {
            term_states[id].input_len--;
            term_states[id].input[term_states[id].input_len] = 0;
        }
        return;
    }
    if (key == 0x0D) { 
        cmdline[0] = '>';
        cmdline[1] = ' ';
        term_str_cpy(&cmdline[2], term_states[id].input, TERM_LINE_LEN);
        term_push_line(id, cmdline);
        term_exec_command(id, term_states[id].input);
        term_states[id].input_len = 0;
        term_states[id].input[0] = 0;
        return;
    }
    if (key >= 32 && key <= 126) {
        if (term_states[id].input_len < TERM_LINE_LEN - 1) {
            term_states[id].input[term_states[id].input_len++] = (char)key;
            term_states[id].input[term_states[id].input_len] = 0;
        }
    }
}

void term_build_output_text(int id, int clear_after) {
    if (id < 0 || id >= term_max_win) return;
    term_clipboard_len = 0;
    for (int i = 0; i < term_states[id].line_count; i++) {
        const char *ln = term_states[id].lines[i];
        int ln_len = str_len(ln);
        if (ln_len <= 0) continue;
        if (term_clipboard_len + ln_len + 1 >= TERM_CLIPBOARD_SIZE) break;
        for (int j = 0; j < ln_len; j++) term_clipboard[term_clipboard_len++] = ln[j];
        term_clipboard[term_clipboard_len++] = '\n';
    }
    if (term_clipboard_len > 0 && term_clipboard[term_clipboard_len - 1] == '\n') term_clipboard_len--;
    term_clipboard[term_clipboard_len] = 0;
    if (clear_after) {
        term_states[id].line_count = 0;
        desktop_needs_full_blit = 1;
    }
}

void term_paste_clipboard_to_input(int id) {
    if (id < 0 || id >= term_max_win) return;
    if (term_clipboard_len <= 0) return;
    if (term_states[id].input_len < 0) term_states[id].input_len = 0;
    int room = (TERM_LINE_LEN - 1) - term_states[id].input_len;
    if (room <= 0) return;
    int n = term_clipboard_len;
    if (n > room) n = room;
    for (int i = 0; i < n; i++) term_states[id].input[term_states[id].input_len + i] = term_clipboard[i];
    term_states[id].input_len += n;
    term_states[id].input[term_states[id].input_len] = 0;
    desktop_needs_full_blit = 1;
}

void handle_terminal_scrollbar_click(int id, int mx, int my, int wins_x, int wins_y, int wins_w, int wins_h) {
    int cx, cy, cw, ch;
    int scrollbar_w = 11;
    int content_h;
    int thumb_y;
    int visible_lines, total_wrapped_lines;
    int thumb_h;
    int click_in_track;
    int scroll_x, scroll_y, scroll_h;
    int char_width = 6;
    int chars_per_line;
    int i;
    
    if (id < 0 || id >= term_max_win) return;
    
    
    cx = wins_x;
    cy = wins_y;
    cw = wins_w;
    ch = wins_h;
    content_h = ch - 4;
    visible_lines = content_h / 8;
    chars_per_line = (cw - scrollbar_w - 2 - 4) / char_width;
    scroll_x = cx + cw - scrollbar_w - 1;
    scroll_y = cy + 2;
    scroll_h = content_h;
    
    
    total_wrapped_lines = 0;
    for (i = 0; i < term_states[id].line_count; i++) {
        int line_len = str_len(term_states[id].lines[i]);
        int wrapped = (line_len + chars_per_line - 1) / chars_per_line;
        if (wrapped < 1) wrapped = 1;
        total_wrapped_lines += wrapped;
    }
    if (total_wrapped_lines < 1) total_wrapped_lines = 1;
    
    if (total_wrapped_lines <= visible_lines) {
        thumb_h = scroll_h - 22;
        thumb_y = scroll_y + 11;
    } else {
        thumb_h = (visible_lines * (scroll_h - 22)) / total_wrapped_lines;
        if (thumb_h < 16) thumb_h = 16;
        thumb_y = scroll_y + 11 + (term_states[id].scroll_y * (scroll_h - 22 - thumb_h)) /
                  (total_wrapped_lines - visible_lines);
    }
    
    
    if (mx < cx || mx >= cx + cw || my < cy || my >= cy + ch) return;
    
    
    if (mx < scroll_x || mx >= scroll_x + scrollbar_w ||
        my < scroll_y || my >= scroll_y + scroll_h) return;
    
    
    if (my >= scroll_y + 2 && my < scroll_y + 12) {
        if (term_states[id].scroll_y > 0) {
            term_states[id].scroll_y--;
            desktop_needs_full_blit = 1;
        }
        term_scrollbar_dragging = -1;
        return;
    }
    
    
    if (my >= scroll_y + scroll_h - 12 && my < scroll_y + scroll_h - 2) {
        if (term_states[id].scroll_y < total_wrapped_lines - visible_lines) {
            term_states[id].scroll_y++;
            desktop_needs_full_blit = 1;
        }
        term_scrollbar_dragging = -1;
        return;
    }
    
    
    click_in_track = (my >= scroll_y + 13 && my < scroll_y + scroll_h - 13);
    
    if (click_in_track && total_wrapped_lines > visible_lines) {
        
        if (my < thumb_y) {
            
            term_states[id].scroll_y -= visible_lines;
            if (term_states[id].scroll_y < 0) term_states[id].scroll_y = 0;
            desktop_needs_full_blit = 1;
        } else if (my >= thumb_y + thumb_h) {
            
            term_states[id].scroll_y += visible_lines;
            if (term_states[id].scroll_y > total_wrapped_lines - visible_lines)
                term_states[id].scroll_y = total_wrapped_lines - visible_lines;
            desktop_needs_full_blit = 1;
        } else {
            
            term_scrollbar_dragging = id;
            term_scrollbar_drag_start_y = my;
            term_scrollbar_drag_start_scroll = term_states[id].scroll_y;
            desktop_needs_full_blit = 1;
        }
    }
}

int term_ctx_menu_width(void) {
    int i, w = 70;
    for (i = 0; i < TERM_CTX_MENU_ITEMS; i++) {
        int tw = str_len(term_ctx_menu_labels[i]) * 6 + 16;
        if (tw > w) w = tw;
    }
    return w;
}

void draw_term_context_menu(void) {
    int i, w, h;
    if (!term_ctx_menu_open) return;
    w = term_ctx_menu_width();
    h = TERM_CTX_MENU_ITEMS * 11 + 6;
    if (term_ctx_menu_x + w > VGA13_WIDTH) term_ctx_menu_x = VGA13_WIDTH - w;
    if (term_ctx_menu_y + h > VGA13_HEIGHT) term_ctx_menu_y = VGA13_HEIGHT - h;
    vga13_fill_rect(term_ctx_menu_x + 1, term_ctx_menu_y + h, w, 1, 0x08);
    vga13_fill_rect(term_ctx_menu_x + w, term_ctx_menu_y + 1, 1, h, 0x08);
    vga13_fill_rect(term_ctx_menu_x, term_ctx_menu_y, w, h, 0x0F);
    
    for (i = term_ctx_menu_x; i <= term_ctx_menu_x + w - 1; i++) {
        vga13_put_pixel(i, term_ctx_menu_y, 0x00);
        vga13_put_pixel(i, term_ctx_menu_y + h - 1, 0x00);
    }
    for (i = term_ctx_menu_y; i <= term_ctx_menu_y + h - 1; i++) {
        vga13_put_pixel(term_ctx_menu_x, i, 0x00);
        vga13_put_pixel(term_ctx_menu_x + w - 1, i, 0x00);
    }
    for (i = 0; i < TERM_CTX_MENU_ITEMS; i++) {
        int iy = term_ctx_menu_y + 2 + i * 11;
        int inv = (i == term_ctx_menu_hover);
        if (inv) vga13_fill_rect(term_ctx_menu_x + 2, iy - 1, w - 4, 10, 0x00);
        vga13_draw_string(term_ctx_menu_x + 8, iy, term_ctx_menu_labels[i], 0x00, 0x0F, inv);
    }
}

int hit_term_context_menu(int mx, int my) {
    int w, h;
    if (!term_ctx_menu_open) return -1;
    w = term_ctx_menu_width();
    h = TERM_CTX_MENU_ITEMS * 11 + 6;
    if (mx < term_ctx_menu_x || mx >= term_ctx_menu_x + w || my < term_ctx_menu_y || my >= term_ctx_menu_y + h) return -1;
    return (my - term_ctx_menu_y - 3) / 11;
}

void term_context_menu_click(int mx, int my) {
    int item = hit_term_context_menu(mx, my);
    
    if (item == 0) {  }
    else if (item == 1) {  }
    else if (item == 2) {  }
    else if (item == 3) {  }
    term_ctx_menu_open = 0;
    term_ctx_menu_hover = -1;
    desktop_needs_full_blit = 1;
}


void app_terminal_init(void) { term_init(MAX_WIN); }
void app_terminal_open(void) { }
