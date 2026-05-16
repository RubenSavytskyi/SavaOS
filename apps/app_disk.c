#include "app_disk.h"
#include "sv_desktop_internal.h"
#include "sv_gfx.h"
#include "string.h"
#include "fs.h"
#include "fat32.h"
#include "ata.h"
#include "keyboard.h"

static const char icon[24][25] = {
    "........................",
    "...BBBBBBBBBBBBBB.......",
    "..BWWBWWWWWWWWWBWB......",
    "..BWWBWWWWWWBWWBWWB.....",
    "..BWWBWWWWWBWBWBWWWB....",
    "..BWWBWWWWWWBWWBWWWWB...",
    "..BWWBWWWWWWWWWBWWWWB...",
    "..BWWBBBBBBBBBBBWWWWB...",
    "..BWWWWWWWWWWWWWWWWWB...",
    "..BWWWWWWWWWWWWWWWWWB...",
    "..BWWWWWWWWWWWWWWWWWB...",
    "..BWBBBBBBBBBBBBBBBWB...",
    "..BWBWWWWWWWWWWWWWBWB...",
    "..BWBWWWWWWWWWWWWWBWB...",
    "..BWBWWWWWWWWWWWWWBWB...",
    "..BWBWWWWWWWWWWWWWBWB...",
    "..BWBWWWWWWWWWWWWWBWB...",
    "..BWBWWWWWWWWWWWWWBWB...",
    "..BWBWWWWWWWWWWWWWBWB...",
    "..BWBWWWWWWWWWWWWWBWB...",
    "..BWBWWWWWWWWWWWWWBWB...",
    "..BBBBBBBBBBBBBBBBBBB...",
    "........................",
    "........................"
};

void handle_disk_key(int id, int key);

extern int str_cmp_simple(const char *a, const char *b);
extern void notepad_open_file(u32 dir_cluster, const char *name);

int  disk_count[MAX_WIN];
int  disk_sel[MAX_WIN];
u32  disk_cwd_cluster[MAX_WIN];
int  disk_parent_cluster[MAX_WIN];
FSDirEnt disk_entries[MAX_WIN][FS_MAX_FILES];
int  disk_clip_valid = 0;
u32  disk_clip_src_dir = 0;
char disk_clip_name[FS_MAX_NAME];

int disk_ctx_menu_open = 0;
int disk_ctx_menu_x = 0;
int disk_ctx_menu_y = 0;
int disk_ctx_menu_hover = -1;
int disk_ctx_target_idx = -1;
#define DISK_CTX_MENU_ITEMS 7
static const char *disk_ctx_menu_labels[DISK_CTX_MENU_ITEMS] = {
    "Open",
    "New Folder",
    "Cut",
    "Copy",
    "Paste",
    "Delete",
    "Close Window"
};

void disk_refresh(int id) {
    if (id < 0 || id >= MAX_WIN) return;
    if (!wins[id].used || wins[id].app != APP_disk) return;
    if (disk_cwd_cluster[id] < 2) disk_cwd_cluster[id] = fs_root_dir_cluster();
    disk_count[id] = fs_list_dir(disk_cwd_cluster[id], disk_entries[id], FS_MAX_FILES);
    if (disk_sel[id] >= disk_count[id]) disk_sel[id] = disk_count[id] - 1;
    if (disk_sel[id] < 0) disk_sel[id] = 0;
}

void disk_make_new_folder_name(int id, char out[FS_MAX_NAME]) {
    int n = 1;
    if (!out) return;
    out[0] = 0;
    for (;;) {
        char tmp[FS_MAX_NAME];
        tmp[0] = 'N'; tmp[1] = 'E'; tmp[2] = 'W'; tmp[3] = 'F'; tmp[4] = 'O'; tmp[5] = 'L'; tmp[6] = 'D';
        tmp[7] = (char)('0' + (n % 10));
        tmp[8] = 0;

        int exists = 0;
        for (int i = 0; i < disk_count[id]; i++) {
            if (kstrcmp(disk_entries[id][i].name, tmp) == 0) { exists = 1; break; }
        }
        if (!exists) { str_cpy(out, tmp, FS_MAX_NAME); return; }
        n++;
        if (n > 9) break;
    }
    str_cpy(out, "NEWFOLD9", FS_MAX_NAME);
}

void disk_copy_set(int id) {
    if (id < 0 || id >= MAX_WIN) return;
    int idx = disk_sel[id];
    if (idx < 0 || idx >= disk_count[id]) return;
    if (disk_entries[id][idx].is_dir) return;
    disk_clip_valid = 1;
    disk_clip_src_dir = disk_cwd_cluster[id];
    str_cpy(disk_clip_name, disk_entries[id][idx].name, FS_MAX_NAME);
}

void disk_sort_entries_by_icon(int id) {
    if (id < 0 || id >= MAX_WIN || wins[id].app != APP_disk) return;

    for (int i = 0; i < disk_count[id]; i++) {
        for (int j = i + 1; j < disk_count[id]; j++) {
            FSDirEnt *a = &disk_entries[id][i];
            FSDirEnt *b = &disk_entries[id][j];
            int swap = 0;
            if (a->is_dir != b->is_dir) {
                if (!a->is_dir && b->is_dir) swap = 1;
            } else if (str_cmp_simple(a->name, b->name) > 0) {
                swap = 1;
            }
            if (swap) {
                FSDirEnt tmp = disk_entries[id][i];
                disk_entries[id][i] = disk_entries[id][j];
                disk_entries[id][j] = tmp;
            }
        }
    }
    desktop_needs_full_blit = 1;
}

void disk_sort_entries_by_mtime(int id) {
    if (id < 0 || id >= MAX_WIN || wins[id].app != APP_disk) return;

    for (int i = 0; i < disk_count[id]; i++) {
        for (int j = i + 1; j < disk_count[id]; j++) {
            u32 ma = disk_entries[id][i].mtime;
            u32 mb = disk_entries[id][j].mtime;
            int swap = 0;
            if (ma < mb) swap = 1;
            else if (ma == mb && str_cmp_simple(disk_entries[id][j].name, disk_entries[id][i].name) < 0)
                swap = 1;
            if (swap) {
                FSDirEnt tmp = disk_entries[id][i];
                disk_entries[id][i] = disk_entries[id][j];
                disk_entries[id][j] = tmp;
            }
        }
    }
    desktop_needs_full_blit = 1;
}

void disk_sort_entries_by_name(int id) {
    if (id < 0 || id >= MAX_WIN || wins[id].app != APP_disk) return;

    for (int i = 0; i < disk_count[id]; i++) {
        for (int j = i + 1; j < disk_count[id]; j++) {
            if (str_cmp_simple(disk_entries[id][j].name, disk_entries[id][i].name) < 0) {
                FSDirEnt tmp = disk_entries[id][i];
                disk_entries[id][i] = disk_entries[id][j];
                disk_entries[id][j] = tmp;
            }
        }
    }
    desktop_needs_full_blit = 1;
}

void disk_paste(int id) {
    if (id < 0 || id >= MAX_WIN) return;
    if (!disk_clip_valid) return;
    (void)fs_copy_file(disk_clip_src_dir, disk_clip_name, disk_cwd_cluster[id], disk_clip_name);
    disk_refresh(id);
    desktop_mark_icons_dirty();
}

void disk_cut_selected(int id) {
    if (id < 0 || id >= MAX_WIN || wins[id].app != APP_disk) return;
    int idx = disk_sel[id];
    if (idx < 0 || idx >= disk_count[id]) return;
    if (disk_entries[id][idx].is_dir) return;
    disk_copy_set(id);
    (void)fs_unlink(disk_cwd_cluster[id], disk_entries[id][idx].name);
    disk_refresh(id);
    desktop_mark_icons_dirty();
    desktop_needs_full_blit = 1;
}

static int disk_ctx_menu_width(void) {
    int i, w = 70;
    for (i = 0; i < DISK_CTX_MENU_ITEMS; i++) {
        int tw = str_len(disk_ctx_menu_labels[i]) * 6 + 16;
        if (tw > w) w = tw;
    }
    return w;
}

void disk_draw_context_menu(void) {
    int i, w, h;
    if (!disk_ctx_menu_open) return;
    w = disk_ctx_menu_width();
    h = DISK_CTX_MENU_ITEMS * 11 + 6;

    if (disk_ctx_menu_x + w > VGA13_WIDTH) disk_ctx_menu_x = VGA13_WIDTH - w;
    if (disk_ctx_menu_y + h > VGA13_HEIGHT) disk_ctx_menu_y = VGA13_HEIGHT - h;

    vga13_fill_rect(disk_ctx_menu_x + 1, disk_ctx_menu_y + h, w, 1, PAL_DARK_GRAY);
    vga13_fill_rect(disk_ctx_menu_x + w, disk_ctx_menu_y + 1, 1, h, PAL_DARK_GRAY);
    vga13_fill_rect(disk_ctx_menu_x, disk_ctx_menu_y, w, h, VGA13_WHITE);

    hline_px(disk_ctx_menu_x, disk_ctx_menu_x + w - 1, disk_ctx_menu_y, VGA13_BLACK);
    hline_px(disk_ctx_menu_x, disk_ctx_menu_x + w - 1, disk_ctx_menu_y + h - 1, VGA13_BLACK);
    vline_px(disk_ctx_menu_x, disk_ctx_menu_y, disk_ctx_menu_y + h - 1, VGA13_BLACK);
    vline_px(disk_ctx_menu_x + w - 1, disk_ctx_menu_y, disk_ctx_menu_y + h - 1, VGA13_BLACK);

    for (i = 0; i < DISK_CTX_MENU_ITEMS; i++) {
        int iy = disk_ctx_menu_y + 2 + i * 11;
        int inv = (i == disk_ctx_menu_hover);
        if (inv) vga13_fill_rect(disk_ctx_menu_x + 2, iy - 1, w - 4, 10, VGA13_BLACK);
        vga13_draw_string(disk_ctx_menu_x + 8, iy, disk_ctx_menu_labels[i], VGA13_BLACK, VGA13_WHITE, inv);
    }
}

int disk_hit_context_menu(int mx, int my) {
    int w, h;
    if (!disk_ctx_menu_open) return -1;
    w = disk_ctx_menu_width();
    h = DISK_CTX_MENU_ITEMS * 11 + 6;
    if (mx < disk_ctx_menu_x || mx >= disk_ctx_menu_x + w || my < disk_ctx_menu_y || my >= disk_ctx_menu_y + h) return -1;
    return (my - disk_ctx_menu_y - 3) / 11;
}

static void disk_ctx_open(int x, int y) {
    disk_ctx_menu_x = x; disk_ctx_menu_y = y;
    disk_ctx_menu_open = 1; disk_ctx_menu_hover = -1; disk_ctx_target_idx = -1;
}
static void disk_ctx_close(void) {
    disk_ctx_menu_open = 0; disk_ctx_menu_hover = -1; disk_ctx_target_idx = -1;
}

static void disk_context_menu_click_impl(int mx, int my);

static void disk_context_menu_click(int mx, int my, int win_id) {
    (void)win_id;
    disk_context_menu_click_impl(mx, my);
}

static void disk_ctx_hover_update(int mx, int my) {
    disk_ctx_menu_hover = disk_hit_context_menu(mx, my);
}

static void disk_context_menu_click_impl(int mx, int my) {
    int item = disk_hit_context_menu(mx, my);
    int active = win_top_id();
    char nm[FS_MAX_NAME];
    if (active < 0 || wins[active].app != APP_disk) {
        disk_ctx_menu_open = 0;
        disk_ctx_menu_hover = -1;
        disk_ctx_target_idx = -1;
        desktop_needs_full_blit = 1;
        return;
    }
    if (item == 0) {
        if (disk_ctx_target_idx >= 0 && disk_ctx_target_idx < disk_count[active]) {
            disk_sel[active] = disk_ctx_target_idx;
            handle_disk_key(active, KEY_ENTER);
        }
    } else if (item == 1) {
        disk_make_new_folder_name(active, nm);
        (void)fs_mkdir(disk_cwd_cluster[active], nm);
        disk_refresh(active);
        desktop_mark_icons_dirty();
    } else if (item == 2) {
        disk_cut_selected(active);
    } else if (item == 3) {
        disk_copy_set(active);
    } else if (item == 4) {
        disk_paste(active);
    } else if (item == 5) {
        if (disk_sel[active] >= 0 && disk_sel[active] < disk_count[active]) {
            handle_disk_key(active, KEY_DELETE);
        }
    } else if (item == 6) {
        win_close(active);
    }

    disk_ctx_menu_open = 0;
    disk_ctx_menu_hover = -1;
    disk_ctx_target_idx = -1;
    desktop_needs_full_blit = 1;
}

void handle_disk_key(int id, int key) {
    if (id < 0 || id >= MAX_WIN || wins[id].app != APP_disk) return;

    if (key == KEY_UP) {
        if (disk_sel[id] > 0) {
            disk_sel[id]--;
            desktop_needs_full_blit = 1;
        }
    } else if (key == KEY_DOWN) {
        if (disk_sel[id] + 1 < disk_count[id]) {
            disk_sel[id]++;
            desktop_needs_full_blit = 1;
        }
    } else if (key == KEY_ENTER) {
        int idx = disk_sel[id];
        if (idx < 0 || idx >= disk_count[id]) return;
        if (disk_entries[id][idx].is_dir) {
            disk_parent_cluster[id] = disk_cwd_cluster[id];
            disk_cwd_cluster[id] = disk_entries[id][idx].first_cluster;
            disk_sel[id] = 0;
            disk_refresh(id);
            desktop_needs_full_blit = 1;
        } else {
            notepad_open_file(disk_cwd_cluster[id], disk_entries[id][idx].name);
        }
    } else if (key == KEY_BACKSP) {
        u32 parent = disk_parent_cluster[id];
        if (parent == 0 && disk_cwd_cluster[id] != fs_root_dir_cluster()) {
            parent = fs_root_dir_cluster();
        }
        if (disk_cwd_cluster[id] != fs_root_dir_cluster()) {
            disk_cwd_cluster[id] = (parent >= 2) ? parent : fs_root_dir_cluster();
            disk_parent_cluster[id] = 0;
            disk_sel[id] = 0;
            disk_refresh(id);
            desktop_needs_full_blit = 1;
        }
    } else if (key == KEY_DELETE) {
        int idx = disk_sel[id];
        if (idx < 0 || idx >= disk_count[id]) return;
        if (disk_entries[id][idx].is_dir) {
            (void)fs_rmdir_empty(disk_cwd_cluster[id], disk_entries[id][idx].name);
        } else {
            (void)fs_unlink(disk_cwd_cluster[id], disk_entries[id][idx].name);
        }
        disk_refresh(id);
        desktop_mark_icons_dirty();
        desktop_needs_full_blit = 1;
    } else if (key == 0x03) {         
        disk_copy_set(id);
        desktop_needs_full_blit = 1;
    } else if (key == 0x18) {         
        disk_cut_selected(id);
        desktop_needs_full_blit = 1;
    } else if (key == 0x16) {         
        disk_paste(id);
        desktop_needs_full_blit = 1;
    }
}

static void draw(int id, int cx, int cy, int cw, int ch) {
    int i, rows, y, name_w, type_x;
    vga13_fill_rect(cx, cy, cw, ch, VGA13_WHITE);
    vga13_fill_rect(cx, cy, cw, 11, PAL_LIGHT_GRAY);
    if (disk_cwd_cluster[id] == fs_root_dir_cluster()) {
        vga13_draw_string(cx + 4, cy + 2, "FAT32", VGA13_BLACK, PAL_LIGHT_GRAY, 0);
    } else {
        vga13_draw_string(cx + 4, cy + 2, "Folder", VGA13_BLACK, PAL_LIGHT_GRAY, 0);
    }
vga13_fill_rect(cx + cw - 12, cy + 1, 9, 9, PAL_DARK_GRAY);
vga13_draw_string(cx + cw - 10, cy + 1, "x", VGA13_WHITE, PAL_DARK_GRAY, 0);
    rows = (ch - 26) / 8;
    if (rows < 1) rows = 1;

    name_w = (cw - 20) / 2;
    type_x = cx + 10 + name_w;

    y = cy + 14;
    for (i = 0; i < rows && i < disk_count[id]; i++) {
        int is_sel = (i == disk_sel[id]);
        if (is_sel) vga13_fill_rect(cx + 2, y - 1, cw - 4, 9, VGA13_BLACK);
        vga13_draw_string(cx + 4, y, disk_entries[id][i].name,
                          is_sel ? VGA13_WHITE : VGA13_BLACK,
                          is_sel ? VGA13_BLACK : VGA13_WHITE, 0);
        if (disk_entries[id][i].is_dir) {
            vga13_draw_string(type_x, y, "<DIR>",
                              is_sel ? VGA13_WHITE : VGA13_BLACK,
                              is_sel ? VGA13_BLACK : VGA13_WHITE, 0);
        }
        y += 8;
    }
}

static void on_open(int id) {
    fs_try_mount_fat32();
    disk_cwd_cluster[id] = fs_root_dir_cluster();
    disk_parent_cluster[id] = 0;
    disk_count[id] = fs_list_dir(disk_cwd_cluster[id], disk_entries[id], FS_MAX_FILES);
    disk_sel[id] = 0;
}

static int overlay_is_open(void) { return disk_ctx_menu_open; }
static void close_overlay(void) { disk_ctx_close(); }

#include "sv_desktop_internal.h"

static void on_click(int id, app_mouse_t *ev) {
    int cx = ev->cx, cy = ev->cy;
    int cw = ev->cw, ch = ev->ch;
    int mx = ev->mx, my = ev->my;

    
if (mx >= cx + cw - 12 && mx < cx + cw - 3 &&
    my >= cy + 1        && my < cy + 10) {
    handle_disk_key(id, KEY_BACKSP);
    return;
}

    int rows = (ch - 24) / 8;
    if (rows < 1) rows = 1;
    int list_y0 = cy + 12;
    int list_y1 = list_y0 + rows * 8;

    if (mx >= cx && mx < cx + cw && my >= list_y0 && my < list_y1) {
        int idx = (my - list_y0) / 8;
        if (idx >= 0 && idx < rows && idx < disk_count[id]) {
            if (disk_sel[id] == idx) {
                
                handle_disk_key(id, KEY_ENTER);
            } else {
                disk_sel[id] = idx;
            }
            desktop_needs_full_blit = 1;
        }
    }
}

static void on_right_click(int id, app_mouse_t *ev) {
    disk_ctx_menu_x = ev->mx;
    disk_ctx_menu_y = ev->my;
    disk_ctx_open(ev->mx, ev->my);
    
    {
        int cx = ev->cx, cy = ev->cy, cw = ev->cw, ch = ev->ch;
        int mx = ev->mx, my = ev->my;
        int rows = (ch - 24) / 8;
        if (rows < 1) rows = 1;
        int list_y0 = cy + 12;
        int list_y1 = list_y0 + rows * 8;
        disk_ctx_target_idx = -1;
        if (mx >= cx && mx < cx + cw && my >= list_y0 && my < list_y1) {
            int idx = (my - list_y0) / 8;
            if (idx >= 0 && idx < rows && idx < disk_count[id]) disk_ctx_target_idx = idx;
        }
    }
    desktop_needs_full_blit = 1;
}

extern const MenuItem menu_sav_items[];

static const MenuItem disk_file_items[] = {
    { "New Folder",   APP_NONE },
    { "Open",         APP_NONE },
    { "Close Window", APP_NONE }
};
static const MenuItem disk_edit_items[] = {
    { "Cut",    APP_NONE },
    { "Copy",   APP_NONE },
    { "Paste",  APP_NONE },
    { "Delete", APP_NONE }
};
static const MenuItem disk_view_items[] = {
    { "By Icon",  APP_NONE },
    { "By Name",  APP_NONE },
    { "By Date",  APP_NONE },
    { "Clean Up", APP_NONE }
};
static const MenuItem disk_special_items[] = {
    { "Eject Disk",  APP_NONE },
    { "Erase Disk",  APP_NONE },
    { "Set Startup", APP_NONE }
};

static Menu disk_menus[] = {
    { "@",       0, 0, menu_sav_items,      4 },
    { "File",    0, 0, disk_file_items,     3 },
    { "Edit",    0, 0, disk_edit_items,     4 },
    { "View",    0, 0, disk_view_items,     4 },
    { "Special", 0, 0, disk_special_items,  3 }
};
#define DISK_MENU_COUNT 5

static int disk_erase_root_files(void) {
    int i;
    if (!fs_using_fat32()) return 0;
    {
        FSDirEnt ents[FS_MAX_FILES];
        int n = fs_list_dir(fs_root_dir_cluster(), ents, FS_MAX_FILES);
        for (i = 0; i < n; i++) {
            if (ents[i].is_dir) continue;
            (void)fs_unlink(fs_root_dir_cluster(), ents[i].name);
        }
    }
    return 1;
}

static int disk_write_startup_file(int id) {
    char line[FS_MAX_NAME];
    int idx;
    if (id < 0 || id >= MAX_WIN || wins[id].app != APP_disk) return 0;
    if (!fs_using_fat32()) return 0;
    idx = disk_sel[id];
    if (idx < 0 || idx >= disk_count[id]) return 0;
    str_cpy(line, disk_entries[id][idx].name, FS_MAX_NAME);
    return fat32_write_file_new_in_dir(fs_root_dir_cluster(), "STARTAPP.TXT",
                                       line, (u32)str_len(line));
}

static int on_menu_action(int id, const char *label) {
    extern void win_close(int);
    if (str_eq(label, "New Folder")) {
        char nm[FS_MAX_NAME];
        disk_make_new_folder_name(id, nm);
        (void)fs_mkdir(disk_cwd_cluster[id], nm);
        disk_refresh(id); desktop_needs_full_blit = 1;
        return 1;
    }
    if (str_eq(label, "Open"))         { handle_disk_key(id, KEY_ENTER); desktop_needs_full_blit = 1; return 1; }
    if (str_eq(label, "Close Window")) { win_close(id); return 1; }
    if (str_eq(label, "Copy"))         { disk_copy_set(id); return 1; }
    if (str_eq(label, "Paste"))          { disk_paste(id); desktop_needs_full_blit = 1; return 1; }
    if (str_eq(label, "Delete"))         { handle_disk_key(id, KEY_DELETE); disk_refresh(id); desktop_needs_full_blit = 1; return 1; }
    if (str_eq(label, "Cut"))            { disk_cut_selected(id); return 1; }
    if (str_eq(label, "By Name"))        { disk_sort_entries_by_name(id); return 1; }
    if (str_eq(label, "By Icon"))        { disk_sort_entries_by_icon(id); return 1; }
    if (str_eq(label, "By Date"))        { disk_sort_entries_by_mtime(id); return 1; }
    if (str_eq(label, "Clean Up"))       { disk_sort_entries_by_name(id); disk_sel[id] = 0; disk_refresh(id); return 1; }
    if (str_eq(label, "Eject Disk"))     { win_close(id); return 1; }
    if (str_eq(label, "Erase Disk")) {
        disk_erase_root_files();
        disk_refresh(id);
        desktop_mark_icons_dirty();
        desktop_needs_full_blit = 1;
        return 1;
    }
    if (str_eq(label, "Set Startup")) {
        (void)disk_write_startup_file(id);
        desktop_needs_full_blit = 1;
        return 1;
    }
    return 0;
}

const app_desc_t app_disk_desc = {
    .kind           = APP_disk,
    .default_title  = "System HD",
    .def_x = 56, .def_y = 30, .def_w = 210, .def_h = 138,
    .icon_bmp       = icon,
    .on_open        = on_open,
    .draw           = draw,
    .on_key         = handle_disk_key,
    .on_click       = on_click,
    .on_right_click = on_right_click,
    .draw_overlay   = disk_draw_context_menu,
    .hit_overlay    = disk_hit_context_menu,
    .hover_overlay  = disk_ctx_hover_update,
    .overlay_is_open = overlay_is_open,
    .close_overlay  = close_overlay,
    .click_overlay  = disk_context_menu_click,
    .menu_bar_menus = disk_menus,
    .menu_bar_count = DISK_MENU_COUNT,
    .on_menu_action = on_menu_action,
};

