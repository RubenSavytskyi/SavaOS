#include "app_trash.h"
#include "sv_desktop_internal.h"
#include "sv_gfx.h"
#include "string.h"
#include "fs.h"
#include "keyboard.h"

static const char icon[24][25] = {
    "........................",
    ".........BBBBB..........",
    "....BBBBBBBBBBBBBBB.....",
    "...BWWWWWWWWWWWWWWWB....",
    "...BBBBBBBBBBBBBBBBB....",
    "....BWWWWWWWWWWWWWB.....",
    "....BWWWWWWWWWWWWWB.....",
    "....BWWBWWBWWBWWBWB.....",
    "....BWBWWBWWBWWBWWB.....",
    "....BWBWWBWWBWWBWWB.....",
    "....BWBWWBWWBWWBWWB.....",
    "....BWBWWBWWBWWBWWB.....",
    "....BWBWWBWWBWWBWWB.....",
    "....BWBWWBWWBWWBWWB.....",
    "....BWBWWBWWBWWBWWB.....",
    "....BWBWWBWWBWWBWWB.....",
    "....BWBWWBWWBWWBWWB.....",
    "....BWBWWBWWBWWBWWB.....",
    "....BWWBWWBWWBWWBWB.....",
    "....BWWWWWWWWWWWWWB.....",
    "....BWWWWWWWWWWWWWB.....",
    ".....BBBBBBBBBBBBB......",
    "........................",
    "........................"
};

#define TRASH_DIR_NAME  "TRASH"
#define MAX_TRASH_FILES 64
#define SCROLLBAR_W     11
#define FONT_H          8
#define FONT_W          6
#define STATUSBAR_H     13
#define BUTTON_H        14
#define TOOLBAR_H       20

typedef struct {
    char filename[FS_MAX_NAME];
    u32  original_dir_cluster;
} trash_entry_t;

static trash_entry_t trash_files[MAX_WIN][MAX_TRASH_FILES];
static int           trash_count[MAX_WIN];
static int           trash_scroll[MAX_WIN];
static int           trash_sel[MAX_WIN];

static int  sb_dragging        = -1;
static int  sb_drag_start_y    = 0;
static int  sb_drag_start_scr  = 0;

static u32 get_or_create_trash_dir(void) {
    u32 root = fs_root_dir_cluster();
    FSDirEnt entries[32];
    int i, count = fs_list_dir(root, entries, 32);
    for (i = 0; i < count; i++)
        if (entries[i].is_dir && str_eq(entries[i].name, TRASH_DIR_NAME))
            return entries[i].first_cluster;
    if (fs_mkdir(root, TRASH_DIR_NAME) == 0) {
        count = fs_list_dir(root, entries, 32);
        for (i = 0; i < count; i++)
            if (entries[i].is_dir && str_eq(entries[i].name, TRASH_DIR_NAME))
                return entries[i].first_cluster;
    }
    return 0;
}

static void read_trash_files(u32 tc, int id) {
    FSDirEnt entries[32];
    int i, count;
    trash_count[id] = 0;
    trash_sel[id]   = -1;

    char info_buf[512];
    int info_n = fs_read_file_in_dir(tc, "INFO.TXT", info_buf, sizeof(info_buf) - 1);
    if (info_n < 0) info_n = 0;
    info_buf[info_n] = 0;

    count = fs_list_dir(tc, entries, 32);
    for (i = 0; i < count && trash_count[id] < MAX_TRASH_FILES; i++) {
        if (entries[i].is_dir || str_eq(entries[i].name, "INFO.TXT")) continue;
        str_cpy(trash_files[id][trash_count[id]].filename, entries[i].name, FS_MAX_NAME);

        u32 orig = fs_root_dir_cluster();
        char *p = info_buf;
        while (*p) {
            char *sep = p;
            while (*sep && *sep != '|') sep++;
            if (*sep == '|') {
                int nl = (int)(sep - p);
                if (nl == str_len(entries[i].name)) {
                    int match = 1, k;
                    for (k = 0; k < nl; k++)
                        if (p[k] != entries[i].name[k]) { match = 0; break; }
                    if (match) {
                        orig = 0;
                        char *num = sep + 1;
                        while (*num >= '0' && *num <= '9')
                            orig = orig * 10 + (u32)(*num++ - '0');
                    }
                }
            }
            while (*p && *p != '\n') p++;
            if (*p == '\n') p++;
        }
        trash_files[id][trash_count[id]].original_dir_cluster = orig;
        trash_count[id]++;
    }
}

static void refresh_trash(int id) {
    u32 tc = get_or_create_trash_dir();
    if (tc) read_trash_files(tc, id);
}

void trash_move_file(u32 dir_cluster, const char *filename) {
    u32 tc = get_or_create_trash_dir();
    if (!tc || !filename) return;
    if (fs_copy_file(dir_cluster, filename, tc, filename) != 0) return;
    fs_unlink(dir_cluster, filename);

    char info_buf[512];
    int n = fs_read_file_in_dir(tc, "INFO.TXT", info_buf, sizeof(info_buf) - 64);
    if (n < 0) n = 0;
    info_buf[n] = 0;
    char line[64];
    ksnprintf(line, sizeof(line), "%s|%u\n", filename, (unsigned)dir_cluster);
    int ll = str_len(line), i;
    for (i = 0; i < ll && n < (int)sizeof(info_buf) - 1; i++)
        info_buf[n++] = line[i];
    info_buf[n] = 0;
    fat32_write_file_new_in_dir(tc, "INFO.TXT", info_buf, (u32)n);
}

static void restore_selected(int id) {
    if (trash_sel[id] < 0 || trash_sel[id] >= trash_count[id]) return;
    u32 tc = get_or_create_trash_dir();
    if (!tc) return;

    int idx = trash_sel[id];
    u32 orig = trash_files[id][idx].original_dir_cluster;
    if (!orig) orig = fs_root_dir_cluster();

    if (fs_copy_file(tc, trash_files[id][idx].filename,
                     orig, trash_files[id][idx].filename) != 0) return;
    fs_unlink(tc, trash_files[id][idx].filename);

    int i;
    for (i = idx; i < trash_count[id] - 1; i++)
        trash_files[id][i] = trash_files[id][i + 1];
    trash_count[id]--;
    trash_sel[id] = -1;

    
    {
        char info_buf[512], new_buf[512];
        int n = fs_read_file_in_dir(tc, "INFO.TXT", info_buf, sizeof(info_buf) - 1);
        if (n > 0) {
            info_buf[n] = 0;
            int ni = 0, skipped = 0;
            char *p = info_buf;
            
            char saved[FS_MAX_NAME];
            str_cpy(saved, trash_files[id][idx].filename, FS_MAX_NAME);
            while (*p) {
                char *ls = p;
                while (*p && *p != '\n') p++;
                if (*p == '\n') p++;
                if (!skipped && str_starts_with(ls, saved)) {
                    skipped = 1;
                } else {
                    int ll = (int)(p - ls), k;
                    for (k = 0; k < ll && ni < 511; k++)
                        new_buf[ni++] = ls[k];
                }
            }
            new_buf[ni] = 0;
            fat32_write_file_new_in_dir(tc, "INFO.TXT", new_buf, (u32)ni);
        }
    }

    desktop_needs_full_blit = 1;
    trash_request_desktop_refresh();
}

static void empty_trash(int id) {
    u32 tc = get_or_create_trash_dir();
    if (!tc) return;
    FSDirEnt entries[32];
    int i, count = fs_list_dir(tc, entries, 32);
    for (i = 0; i < count; i++)
        if (!entries[i].is_dir) fs_unlink(tc, entries[i].name);
    trash_count[id]  = 0;
    trash_sel[id]    = -1;
    trash_scroll[id] = 0;
    desktop_needs_full_blit = 1;
}

static void on_open(int id) {
    if (id < 0 || id >= MAX_WIN) return;
    trash_count[id]  = 0;
    trash_scroll[id] = 0;
    trash_sel[id]    = -1;
    refresh_trash(id);
}

static void draw(int id, int cx, int cy, int cw, int ch) {
    if (id < 0 || id >= MAX_WIN) return;

    vga13_fill_rect(cx, cy, cw, ch, VGA13_WHITE);

    
    vga13_fill_rect(cx, cy, cw, TOOLBAR_H, PAL_LIGHT_GRAY);
    

    
    {
        char lbl[20];
        ksnprintf(lbl, sizeof(lbl), "%d item%s",
                  trash_count[id], trash_count[id] == 1 ? "" : "s");
        vga13_draw_string(cx + 4, cy + 6, lbl, VGA13_BLACK, PAL_LIGHT_GRAY, 0);
    }

    
    {
        int bw = 40, bh = BUTTON_H;
        int bx = cx + cw - bw - SCROLLBAR_W - 5;
        int by = cy + (TOOLBAR_H - bh) / 2;
        u8 fg = (trash_count[id] == 0) ? PAL_DARK_GRAY : VGA13_BLACK;
        vga13_fill_rect(bx, by, bw, bh, VGA13_WHITE);
        hline_px(bx, bx + bw - 1, by,          VGA13_BLACK);
        hline_px(bx, bx + bw - 1, by + bh - 1, VGA13_BLACK);
        vline_px(bx, by, by + bh - 1,            VGA13_BLACK);
        vline_px(bx + bw - 1, by, by + bh - 1,   VGA13_BLACK);
        vga13_draw_string(bx + (bw - 5 * FONT_W) / 2, by + (bh - FONT_H) / 2,
                          "Empty", fg, VGA13_WHITE, 0);
    }

    
    {
        int bw = 52, bh = BUTTON_H;
        int empty_bx = cx + cw - 40 - SCROLLBAR_W - 5;
        int bx = empty_bx - bw - 4;
        int by = cy + (TOOLBAR_H - bh) / 2;
        u8 fg = (trash_sel[id] < 0) ? PAL_DARK_GRAY : VGA13_BLACK;
        vga13_fill_rect(bx, by, bw, bh, VGA13_WHITE);
        hline_px(bx, bx + bw - 1, by,          VGA13_BLACK);
        hline_px(bx, bx + bw - 1, by + bh - 1, VGA13_BLACK);
        vline_px(bx, by, by + bh - 1,            VGA13_BLACK);
        vline_px(bx + bw - 1, by, by + bh - 1,   VGA13_BLACK);
        vga13_draw_string(bx + (bw - 7 * FONT_W) / 2, by + (bh - FONT_H) / 2,
                          "Restore", fg, VGA13_WHITE, 0);
    }

    
    int margin_top = 4; 

    int list_y = cy + TOOLBAR_H + margin_top; 
    int list_h = ch - TOOLBAR_H - STATUSBAR_H - margin_top;
    int list_w = cw - SCROLLBAR_W - 1;
    int visible = list_h / FONT_H;

    int max_scroll = trash_count[id] - visible;
    if (max_scroll < 0) max_scroll = 0;
    if (trash_scroll[id] > max_scroll) trash_scroll[id] = max_scroll;
    if (trash_scroll[id] < 0)         trash_scroll[id] = 0;

int y = list_y, i;
    for (i = trash_scroll[id]; i < trash_count[id] && y < list_y + list_h; i++) {
        int sel = (i == trash_sel[id]);
        if (sel)
            
            vga13_fill_rect(cx + 2, y - 1, list_w - 4, FONT_H + 1, VGA13_BLACK);
        vga13_draw_string(cx + 4, y,
                          trash_files[id][i].filename,
                          sel ? VGA13_WHITE : VGA13_BLACK,
                          sel ? VGA13_BLACK : VGA13_WHITE, 0);
        y += FONT_H;
    }

    
    int sx = cx + cw - SCROLLBAR_W - 1;
    int sy = list_y;
    int sh = list_h;

    int thumb_h, thumb_y;
    if (trash_count[id] <= visible) {
        thumb_h = sh - 22;
        thumb_y = sy + 11;
    } else {
        thumb_h = (visible * (sh - 22)) / trash_count[id];
        if (thumb_h < 12) thumb_h = 12;
        thumb_y = (max_scroll > 0)
            ? sy + 11 + (trash_scroll[id] * (sh - 22 - thumb_h)) / max_scroll
            : sy + 11;
    }

    vga13_fill_rect(sx, sy, SCROLLBAR_W, sh, PAL_LIGHT_GRAY);
    hline_px(sx, sx + SCROLLBAR_W - 1, sy,          VGA13_BLACK);
    hline_px(sx, sx + SCROLLBAR_W - 1, sy + sh - 1, VGA13_BLACK);
    vline_px(sx, sy, sy + sh - 1,                    VGA13_BLACK);
    vline_px(sx + SCROLLBAR_W - 1, sy, sy + sh - 1,  VGA13_BLACK);

    vga13_fill_rect(sx + 1, sy + 1, SCROLLBAR_W - 2, 10, VGA13_WHITE);
    hline_px(sx + 1, sx + SCROLLBAR_W - 2, sy + 1,  VGA13_BLACK);
    hline_px(sx + 1, sx + SCROLLBAR_W - 2, sy + 10, VGA13_BLACK);
    vline_px(sx + 1, sy + 1, sy + 10,                VGA13_BLACK);
    vline_px(sx + SCROLLBAR_W - 2, sy + 1, sy + 10,  VGA13_BLACK);
    vga13_draw_string(sx + 3, sy + 2, "\x1E", VGA13_BLACK, VGA13_WHITE, 0);

    vga13_fill_rect(sx + 1, sy + sh - 11, SCROLLBAR_W - 2, 10, VGA13_WHITE);
    hline_px(sx + 1, sx + SCROLLBAR_W - 2, sy + sh - 11, VGA13_BLACK);
    hline_px(sx + 1, sx + SCROLLBAR_W - 2, sy + sh - 1,  VGA13_BLACK);
    vline_px(sx + 1, sy + sh - 11, sy + sh - 1,           VGA13_BLACK);
    vline_px(sx + SCROLLBAR_W - 2, sy + sh - 11, sy + sh - 1, VGA13_BLACK);
    vga13_draw_string(sx + 3, sy + sh - 10, "\x1F", VGA13_BLACK, VGA13_WHITE, 0);

    vga13_fill_rect(sx + 2, thumb_y, SCROLLBAR_W - 4, thumb_h, PAL_LIGHT_GRAY);
    hline_px(sx + 2, sx + SCROLLBAR_W - 3, thumb_y,               VGA13_BLACK);
    hline_px(sx + 2, sx + SCROLLBAR_W - 3, thumb_y + thumb_h - 1, VGA13_BLACK);
    vline_px(sx + 2, thumb_y, thumb_y + thumb_h - 1,               VGA13_BLACK);
    vline_px(sx + SCROLLBAR_W - 3, thumb_y, thumb_y + thumb_h - 1, VGA13_BLACK);
    hline_px(sx + 3, sx + SCROLLBAR_W - 4, thumb_y + 1, VGA13_WHITE);
    vline_px(sx + 3, thumb_y + 1, thumb_y + thumb_h - 2, VGA13_WHITE);

    

    int status_y = cy + ch - STATUSBAR_H;
    vga13_fill_rect(cx, status_y, cw, STATUSBAR_H, PAL_LIGHT_GRAY);
    

    {
        
        if (trash_sel[id] >= 0 && trash_sel[id] < trash_count[id]) {
            char status[FS_MAX_NAME + 16];
            ksnprintf(status, sizeof(status), "%s selected", trash_files[id][trash_sel[id]].filename);
            vga13_draw_string(cx + 4, status_y + 3, status, VGA13_BLACK, PAL_LIGHT_GRAY, 0);
        } else {
            vga13_draw_string(cx + 4, status_y + 3, "0 files selected", VGA13_BLACK, PAL_LIGHT_GRAY, 0);
        }
    }
}

static void on_click(int id, app_mouse_t *ev) {
    if (id < 0 || id >= MAX_WIN || !ev) return;
    if (ev->button != 0) return;

    int cx = wins[id].x + 3;
    int cy = wins[id].y + SVS_TITLE_H + 1;
    int cw = wins[id].w - 6;
    int ch = wins[id].h - SVS_TITLE_H - 3;
    int mx = ev->mx, my = ev->my;

    int list_h  = ch - TOOLBAR_H - STATUSBAR_H;
    int list_w  = cw - SCROLLBAR_W - 1;
    int visible = list_h / FONT_H;
    int max_scroll = trash_count[id] - visible;
    if (max_scroll < 0) max_scroll = 0;

    
    if (my >= cy && my < cy + TOOLBAR_H) {
        int bh = BUTTON_H;
        int by = cy + (TOOLBAR_H - bh) / 2;
        int empty_bx   = cx + cw - 40 - SCROLLBAR_W - 5;
        int restore_bx = empty_bx - 46 - 4;
        if (mx >= empty_bx && mx < empty_bx + 40 && my >= by && my < by + bh) {
            empty_trash(id); return;
        }
        if (mx >= restore_bx && mx < restore_bx + 46 && my >= by && my < by + bh) {
            restore_selected(id); return;
        }
        return;
    }

    
    int sx = cx + cw - SCROLLBAR_W - 1;
    int sy = cy + TOOLBAR_H;
    int sh = list_h;

    int thumb_h, thumb_y;
    if (trash_count[id] <= visible) {
        thumb_h = sh - 22; thumb_y = sy + 11;
    } else {
        thumb_h = (visible * (sh - 22)) / trash_count[id];
        if (thumb_h < 12) thumb_h = 12;
        thumb_y = (max_scroll > 0)
            ? sy + 11 + (trash_scroll[id] * (sh - 22 - thumb_h)) / max_scroll
            : sy + 11;
    }

    if (mx >= sx && mx < sx + SCROLLBAR_W && my >= sy && my < sy + sh) {
        if (my >= sy + 1 && my <= sy + 10) {
            if (trash_scroll[id] > 0) trash_scroll[id]--;
            desktop_needs_full_blit = 1; return;
        }
        if (my >= sy + sh - 11 && my <= sy + sh - 1) {
            if (trash_scroll[id] < max_scroll) trash_scroll[id]++;
            desktop_needs_full_blit = 1; return;
        }
        if (my < thumb_y) {
            trash_scroll[id] -= visible;
            if (trash_scroll[id] < 0) trash_scroll[id] = 0;
            desktop_needs_full_blit = 1; return;
        }
        if (my >= thumb_y + thumb_h) {
            trash_scroll[id] += visible;
            if (trash_scroll[id] > max_scroll) trash_scroll[id] = max_scroll;
            desktop_needs_full_blit = 1; return;
        }
        sb_dragging      = id;
        sb_drag_start_y  = my;
        sb_drag_start_scr = trash_scroll[id];
        return;
    }

    
int margin_top = 4; 

    int list_y = cy + TOOLBAR_H + margin_top; 

    if (mx >= cx && mx < cx + list_w && my >= list_y && my < list_y + list_h) {
        int idx = (my - list_y) / FONT_H + trash_scroll[id];
        if (idx >= 0 && idx < trash_count[id]) {
            if (trash_sel[id] == idx) {
                restore_selected(id); return;
            }
            trash_sel[id] = idx;
        } else {
            trash_sel[id] = -1;
        }
        desktop_needs_full_blit = 1;
    }
}

static void on_drag(int id, app_mouse_t *ev) {
    if (id < 0 || id >= MAX_WIN || !ev) return;
    if (sb_dragging != id) return;

    int ch      = wins[id].h - SVS_TITLE_H - 3;
    int list_h  = ch - TOOLBAR_H - STATUSBAR_H;
    int visible = list_h / FONT_H;
    int sh      = list_h;
    int max_scroll = trash_count[id] - visible;
    if (max_scroll < 0) max_scroll = 0;

    int thumb_h;
    if (trash_count[id] <= visible) {
        thumb_h = sh - 22;
    } else {
        thumb_h = (visible * (sh - 22)) / trash_count[id];
        if (thumb_h < 12) thumb_h = 12;
    }

    int track_h = sh - 22 - thumb_h;
    if (track_h < 1) track_h = 1;
    if (max_scroll < 1) { sb_dragging = -1; return; }

    int new_scr = sb_drag_start_scr + (ev->my - sb_drag_start_y) * max_scroll / track_h;
    if (new_scr < 0)          new_scr = 0;
    if (new_scr > max_scroll) new_scr = max_scroll;
    if (trash_scroll[id] != new_scr) {
        trash_scroll[id] = new_scr;
        desktop_needs_full_blit = 1;
    }
}

static void on_release(int id, app_mouse_t *ev) {
    (void)id; (void)ev;
    sb_dragging = -1;
}

static void trash_key(int id, int key) {
    if (id < 0 || id >= MAX_WIN) return;
    int ch      = wins[id].h - SVS_TITLE_H - 3;
    int list_h  = ch - TOOLBAR_H - STATUSBAR_H;
    int visible = list_h / FONT_H;

    if (key == KEY_UP) {
        if (trash_sel[id] > 0) {
            trash_sel[id]--;
            if (trash_sel[id] < trash_scroll[id])
                trash_scroll[id] = trash_sel[id];
            desktop_needs_full_blit = 1;
        }
    } else if (key == KEY_DOWN) {
        if (trash_sel[id] < trash_count[id] - 1) {
            trash_sel[id]++;
            if (trash_sel[id] >= trash_scroll[id] + visible)
                trash_scroll[id] = trash_sel[id] - visible + 1;
            desktop_needs_full_blit = 1;
        }
    } else if (key == KEY_ENTER) {
        restore_selected(id);
    } else if (key == KEY_DELETE) {
        empty_trash(id);
    }
}

extern const MenuItem menu_sav_items[];

static const MenuItem trash_file_items[] = {
    { "Restore",     APP_NONE },
    { "Empty Trash", APP_NONE },
};

static Menu trash_menus[] = {
    { "@",    0, 0, menu_sav_items,   4 },
    { "File", 0, 0, trash_file_items, 2 },
};

static int on_menu_action(int id, const char *label) {
    if (str_eq(label, "Restore"))     { restore_selected(id); return 1; }
    if (str_eq(label, "Empty Trash")) { empty_trash(id);      return 1; }
    return 0;
}

const app_desc_t app_trash_desc = {
    .kind           = APP_trash,
    .default_title  = "Trash",
    .def_x = 90, .def_y = 45, .def_w = 200, .def_h = 150,
    .icon_bmp       = icon,
    .on_open        = on_open,
    .draw           = draw,
    .on_key         = trash_key,
    .on_click       = on_click,
    .on_drag        = on_drag,
    .on_release     = on_release,
    .menu_bar_menus = trash_menus,
    .menu_bar_count = 2,
    .on_menu_action = on_menu_action,
};
