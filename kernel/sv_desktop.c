

#include "types.h"
#include "sv_gfx.h"
#include "sv_desktop.h"
#include "sv_desktop_internal.h"
#include "mouse.h"
#include "keyboard.h"
#include "timer.h"
#include "rtc.h"
#include "string.h"
#include "fs.h"
#include "fat32.h"
#include "ata.h"
#include "app_registry.h"
#include "app_control_panel.h"
#include "app_trash.h"

void hline_px(int x1, int x2, int y, u8 c);
void vline_px(int x, int y1, int y2, u8 c);
int  win_top_id(void);
void bring_to_front(int win_idx);
int  ksnprintf(char *buf, int cap, const char *fmt, ...);
void str_cpy(char *d, const char *s, int max);
int  str_len(const char *s);
int  str_eq(const char *a, const char *b);
int  str_starts_with(const char *s, const char *pfx);
void desktop_mark_icons_dirty(void);
void win_close(int id);
void app_open(app_kind_t app);
void layout_menus(void);

static void win_open(app_kind_t app, const char *title, int x, int y, int w, int h);
static void switch_menus_for_app(app_kind_t app);
static void frame_loop(void);
static int  hit_top_window(int mx, int my);
static void process_drag(int mx, int my);
static void process_icon_drag(int mx, int my);
static void desktop_manager_click(int mx, int my, int left_edge);
static int  str_cmp_simple(const char *a, const char *b);
static void win_clamp(int id);
static int  hit_close(int id, int mx, int my);
static int  hit_title_drag(int id, int mx, int my);
static int  win_max_z(void);
static void win_bring_front(int id);
static void wm_draw_all(void);
static int  any_window_animating(void);

void open_disk_at_cluster(u32 dir_cluster, const char *title);
void trash_request_desktop_refresh(void) {
    desktop_mark_icons_dirty();
}

#define DESKTOP_Y        20
#define DESKTOP_H        (VGA13_HEIGHT - DESKTOP_Y)
#define ICON_SIZE        24
#define ICON_LABEL_H     10
#define DBL_CLICK_TICKS  35
#define DBL_CLICK_DIST   4
#define WIN_ANIM_FRAMES  10
#define WIN_ANIM_DELAY_MS 39

typedef struct {
    char label[FS_MAX_NAME];
    app_kind_t app;
    int x, y;
    u32 parent_dir_cluster;
    u32 first_cluster;
    int is_dir;
    u32 mtime;
} icon_t;

win_t wins[MAX_WIN];
static int z_seq;
static int drag_id, drag_off_x, drag_off_y;
static u8  prev_buttons;
int desktop_needs_full_blit = 1;
int desktop_icons_dirty     = 1;

void desktop_mark_icons_dirty(void) { desktop_icons_dirty = 1; }

#define MAX_DESKTOP_ICONS 64
static icon_t  desktop_icons_data[MAX_DESKTOP_ICONS];
static icon_t *desktop_icons      = desktop_icons_data;
static int     desktop_icon_count = 0;
static u32     desktop_folder_cluster = 0;

static int selected_icons[MAX_DESKTOP_ICONS];
static int num_selected_icons = 0;
static int selected_icon      = -1;
static int shift_held         = 0;

static int desktop_clip_valid    = 0;
static int desktop_clip_move     = 0;
static u32 desktop_clip_src_dir  = 0;
static int desktop_clip_is_dir   = 0;
static char desktop_clip_name[FS_MAX_NAME];

static int icon_dragging  = 0;
static int icon_drag_idx  = -1;
static int icon_drag_off_x = 0;
static int icon_drag_off_y = 0;

static int lasso_active  = 0;
static int lasso_start_x = 0, lasso_start_y = 0;
static int lasso_cur_x   = 0, lasso_cur_y   = 0;

static int ctx_menu_open       = 0;
static int ctx_menu_x          = 0, ctx_menu_y = 0;
static int ctx_menu_hover      = -1;
static int ctx_menu_target_icon = -1;

#define CTX_MENU_ITEMS 6
static const char *ctx_menu_labels[CTX_MENU_ITEMS] = {
    "New Folder", "Open", "Get Info", "Rename", "Move to Trash", "Eject"
};

static int  last_click_icon  = -1;
static u32  last_click_ticks = 0;
static int  last_click_x = 0, last_click_y = 0;
static int  last_click_disk_sel    = -1;
static u32  last_click_disk_ticks  = 0;
static int  last_click_disk_x = 0, last_click_disk_y = 0;

static int menu_open  = -1;
static int menu_hover = -1;
static int menu_hot   = -1;

const MenuItem menu_sav_items[] = {
    { "About SavaOS",  APP_about        },
    { "Control Panel", APP_control_panel },
    { "Puzzle",        APP_puzzle        },
    { "Pong",        APP_pong        },
    { "Browser",       APP_browser      },
    { "Shut Down",     APP_NONE         }
};

static const MenuItem menu_desk_file_items[] = {
    { "New Folder", APP_NONE }, { "Open",  APP_NONE },
    { "Close",      APP_NONE }, { "Get Info", APP_NONE },
    { "Duplicate",  APP_NONE }
};
static const MenuItem menu_desk_edit_items[] = {
    { "Undo", APP_NONE }, { "Cut",  APP_NONE }, { "Copy",  APP_NONE },
    { "Paste",APP_NONE }, { "Select All", APP_NONE }
};
static const MenuItem menu_desk_view_items[] = {
    { "By Icon", APP_NONE }, { "By Name", APP_NONE },
    { "By Date", APP_NONE }, { "Clean Up", APP_NONE }
};
static const MenuItem menu_desk_special_items[] = {
    { "Eject Disk", APP_NONE }, { "Erase Disk", APP_NONE },
    { "Set Startup", APP_NONE }
};
static Menu desk_menus[] = {
    { "@",       0,0, menu_sav_items,          SV_MENU_SAV_COUNT },
    { "File",    0,0, menu_desk_file_items,    5 },
    { "Edit",    0,0, menu_desk_edit_items,    5 },
    { "View",    0,0, menu_desk_view_items,    4 },
    { "Special", 0,0, menu_desk_special_items, 3 }
};
#define DESK_MENU_COUNT 5

static Menu sav_only_menus[] = {
    { "@", 0,0, menu_sav_items, SV_MENU_SAV_COUNT }
};
#define SAV_ONLY_COUNT 1

static Menu        *current_menus      = desk_menus;
static int          current_menu_count = DESK_MENU_COUNT;
static app_kind_t   current_menu_app   = APP_NONE;
static Menu        *menus              = desk_menus;
#define MENU_COUNT current_menu_count

void str_cpy(char *d, const char *s, int max) {
    int i;
    for (i = 0; i < max - 1 && s[i]; i++) d[i] = s[i];
    d[i] = 0;
}
int str_len(const char *s) { int i = 0; while (s && s[i]) i++; return i; }
int str_eq(const char *a, const char *b) {
    int i = 0;
    while (a[i] && b[i]) { if (a[i] != b[i]) return 0; i++; }
    return a[i] == 0 && b[i] == 0;
}
int str_starts_with(const char *s, const char *pfx) {
    int i = 0;
    while (pfx[i]) { if (s[i] != pfx[i]) return 0; i++; }
    return 1;
}
static int str_cmp_simple(const char *a, const char *b) {
    if (!a) a = ""; if (!b) b = "";
    for (int i = 0; a[i] || b[i]; i++) {
        unsigned char ca = (unsigned char)a[i];
        unsigned char cb = (unsigned char)b[i];
        if (ca != cb) return (int)ca - (int)cb;
        if (!a[i] && !b[i]) break;
    }
    return 0;
}

void hline_px(int x1, int x2, int y, u8 c) {
    int x, t;
    if (y < 0 || y >= VGA13_HEIGHT) return;
    if (x1 > x2) { t = x1; x1 = x2; x2 = t; }
    if (x1 < 0) x1 = 0;
    if (x2 >= VGA13_WIDTH) x2 = VGA13_WIDTH - 1;
    for (x = x1; x <= x2; x++) vga13_put_pixel(x, y, c);
}
void vline_px(int x, int y1, int y2, u8 c) {
    int y, t;
    if (x < 0 || x >= VGA13_WIDTH) return;
    if (y1 > y2) { t = y1; y1 = y2; y2 = t; }
    if (y1 < 0) y1 = 0;
    if (y2 >= VGA13_HEIGHT) y2 = VGA13_HEIGHT - 1;
    for (y = y1; y <= y2; y++) vga13_put_pixel(x, y, c);
}

static void win_clamp(int id) {
    if (id < 0 || id >= MAX_WIN || !wins[id].used) return;
    if (wins[id].x < 0) wins[id].x = 0;
    if (wins[id].y < DESKTOP_Y) wins[id].y = DESKTOP_Y;
    if (wins[id].x + wins[id].w > VGA13_WIDTH)  wins[id].x = VGA13_WIDTH  - wins[id].w;
    if (wins[id].y + wins[id].h > VGA13_HEIGHT) wins[id].y = VGA13_HEIGHT - wins[id].h;
}

static int hit_top_window(int mx, int my) {
    int i, best = -1, best_z = -1;
    for (i = 0; i < MAX_WIN; i++) {
        if (!wins[i].used) continue;
        if (mx >= wins[i].x && mx < wins[i].x + wins[i].w &&
            my >= wins[i].y && my < wins[i].y + wins[i].h) {
            if (wins[i].z > best_z) { best_z = wins[i].z; best = i; }
        }
    }
    return best;
}

static int win_max_z(void) {
    int m = 0, i;
    for (i = 0; i < MAX_WIN; i++) if (wins[i].used && wins[i].z > m) m = wins[i].z;
    return m;
}
int win_top_id(void) {
    int i, best = -1, best_z = -1;
    for (i = 0; i < MAX_WIN; i++)
        if (wins[i].used && wins[i].z > best_z) { best_z = wins[i].z; best = i; }
    return best;
}
static void win_bring_front(int id) {
    if (id < 0 || id >= MAX_WIN || !wins[id].used) return;
    wins[id].z = win_max_z() + 1;
    switch_menus_for_app(wins[id].app);
}
void bring_to_front(int win_idx) { win_bring_front(win_idx); }

static void win_start_open_anim(int id, int ix, int iy, int x, int y, int w, int h) {
    if (id < 0 || id >= MAX_WIN) return;
    wins[id].animating = 1;
    wins[id].anim_frame = 0;
    wins[id].anim_dir = 1;
    wins[id].anim_sx = ix + ICON_SIZE / 2; wins[id].anim_sy = iy + ICON_SIZE / 2;
    wins[id].anim_sw = 8;                  wins[id].anim_sh = 8;
    wins[id].anim_ex = x;                  wins[id].anim_ey = y;
    wins[id].anim_ew = w;                  wins[id].anim_eh = h;
}
static void desktop_ensure_icons_built(void);
static void win_start_close_anim(int id) {
    int i;
    if (id < 0 || id >= MAX_WIN || !wins[id].used) return;
    desktop_ensure_icons_built();
    wins[id].anim_ex = wins[id].x + wins[id].w / 2;
    wins[id].anim_ey = wins[id].y + wins[id].h / 2;
    wins[id].anim_ew = 8; wins[id].anim_eh = 8;
    for (i = 0; i < desktop_icon_count; i++) {
        if (desktop_icons[i].app == wins[id].app) {
            wins[id].anim_ex = desktop_icons[i].x + ICON_SIZE / 2;
            wins[id].anim_ey = desktop_icons[i].y + ICON_SIZE / 2;
            break;
        }
    }
    wins[id].anim_sx = wins[id].x; wins[id].anim_sy = wins[id].y;
    wins[id].anim_sw = wins[id].w; wins[id].anim_sh = wins[id].h;
    wins[id].animating  = 1;
    wins[id].anim_frame = 0;
    wins[id].anim_dir   = -1;
}

void win_close(int id) {
    if (id >= 0 && id < MAX_WIN && wins[id].used && !wins[id].animating) {
        const app_desc_t *desc = app_registry_get(wins[id].app);
        if (desc && desc->on_close) desc->on_close(id);
        win_start_close_anim(id);
        desktop_needs_full_blit = 1;
    }
}

static void win_open(app_kind_t app, const char *title, int x, int y, int w, int h) {
    int i, slot = -1;
    int ix = x, iy = y;
    for (i = 0; i < MAX_WIN; i++) if (!wins[i].used) { slot = i; break; }
    if (slot < 0) return;

    desktop_ensure_icons_built();
    for (i = 0; i < desktop_icon_count; i++) {
        if (desktop_icons[i].app == app && app != APP_NONE) {
            ix = desktop_icons[i].x; iy = desktop_icons[i].y; break;
        }
    }

    wins[slot].used = 1;
    wins[slot].x = x; wins[slot].y = y;
    wins[slot].w = w; wins[slot].h = h;
    wins[slot].app = app;
    str_cpy(wins[slot].title, title, sizeof(wins[slot].title));
    wins[slot].z = ++z_seq;
    win_clamp(slot);
    win_start_open_anim(slot, ix, iy, x, y, w, h);

    {
        const app_desc_t *desc = app_registry_get(app);
        if (desc && desc->on_open) desc->on_open(slot);
    }
    desktop_needs_full_blit = 1;
}

void app_open(app_kind_t app) {
    const app_desc_t *desc = app_registry_get(app);
    if (!desc) return;
    win_open(app, desc->default_title, desc->def_x, desc->def_y, desc->def_w, desc->def_h);
}

void open_disk_at_cluster(u32 dir_cluster, const char *title) {
    extern int  disk_sel[];
    extern u32  disk_cwd_cluster[];
    extern int  disk_parent_cluster[];
    extern void disk_refresh(int id);
    if (dir_cluster < 2) return;
    win_open(APP_disk, title ? title : "Disk", 60, 40, 200, 140);
    int id = win_top_id();
    if (id >= 0 && wins[id].app == APP_disk) {
        disk_cwd_cluster[id]    = dir_cluster;
        disk_parent_cluster[id] = 0;
        disk_sel[id]            = 0;
        disk_refresh(id);
    }
    desktop_needs_full_blit = 1;
}

extern void notepad_open_file(u32 dir_cluster, const char *name);

void layout_menus(void) {
    int i, x = 4;
    menus = current_menus;
    for (i = 0; i < MENU_COUNT; i++) {
        menus[i].x = x;
        menus[i].w = str_len(menus[i].title) * 6 + 10;
        x += menus[i].w + 2;
    }
}

static void switch_menus_for_app(app_kind_t app) {
    const app_desc_t *desc;
    if (current_menu_app == app) return;
    current_menu_app   = app;
    menu_open  = -1;
    menu_hover = -1;
    menu_hot   = -1;

    desc = app_registry_get(app);
    if (desc && desc->menu_bar_menus && desc->menu_bar_count > 0) {
        current_menus      = (Menu *)desc->menu_bar_menus;
        current_menu_count = desc->menu_bar_count;
    } else if (app == APP_NONE) {
        current_menus      = desk_menus;
        current_menu_count = DESK_MENU_COUNT;
    } else {
        
        current_menus      = sav_only_menus;
        current_menu_count = SAV_ONLY_COUNT;
    }
    menus = current_menus;
    layout_menus();
    desktop_needs_full_blit = 1;
}

static int dropdown_w(int id) {
    int i, w = 50;
    for (i = 0; i < menus[id].item_count; i++) {
        int tw = str_len(menus[id].items[i].label) * 6 + 16;
        if (tw > w) w = tw;
    }
    return w;
}

static int hit_menu_title(int mx, int my) {
    int i;
    if (my < 0 || my >= SVS_MENU_BAR_H) return -1;
    for (i = 0; i < MENU_COUNT; i++)
        if (mx >= menus[i].x && mx < menus[i].x + menus[i].w) return i;
    return -1;
}

static int hit_menu_item(int mx, int my) {
    int x, y, w, h, idx;
    if (menu_open < 0) return -1;
    x = menus[menu_open].x;
    y = SVS_MENU_BAR_H;
    w = dropdown_w(menu_open);
    h = menus[menu_open].item_count * 11 + 6;
    if (mx < x || mx >= x + w || my < y || my >= y + h) return -1;
    idx = (my - y - 3) / 11;
    if (idx < 0 || idx >= menus[menu_open].item_count) return -1;
    return idx;
}

static void desktop_add_icon_app(const char *label, app_kind_t app, int x, int y) {
    if (desktop_icon_count >= MAX_DESKTOP_ICONS) return;
    icon_t *ic = &desktop_icons[desktop_icon_count++];
    kmemset(ic, 0, sizeof(*ic));
    str_cpy(ic->label, label, FS_MAX_NAME);
    ic->app = app; ic->x = x; ic->y = y;
    ic->mtime = 0xFFFFFFFFu;
}
static void desktop_add_icon_fs(const char *label, u32 parent_dir,
                                 u32 first_cluster, int is_dir, int x, int y, u32 mtime) {
    if (desktop_icon_count >= MAX_DESKTOP_ICONS) return;
    icon_t *ic = &desktop_icons[desktop_icon_count++];
    kmemset(ic, 0, sizeof(*ic));
    str_cpy(ic->label, label, FS_MAX_NAME);
    ic->app                = APP_NONE;
    ic->parent_dir_cluster = parent_dir;
    ic->first_cluster      = first_cluster;
    ic->is_dir             = is_dir;
    ic->mtime              = mtime;
    ic->x = x; ic->y = y;
}

static void desktop_build_icons(void) {
    icon_t old[MAX_DESKTOP_ICONS];
    int old_n = desktop_icon_count;
    for (int i = 0; i < old_n; i++) old[i] = desktop_icons[i];
    desktop_icon_count = 0;
    desktop_folder_cluster = 0;

    desktop_add_icon_app("Computer", APP_about,    16,  22);
    desktop_add_icon_app("Notepad",  APP_notepad,  16,  62);
    desktop_add_icon_app("Calc",     APP_calc,     16, 102);
    desktop_add_icon_app("Term",     APP_terminal, 16, 142);
    desktop_add_icon_app("Browser",  APP_browser,  65, 22);
    desktop_add_icon_app("HD",       APP_disk,     VGA13_WIDTH - 31,  22);
    desktop_add_icon_app("Trash",    APP_trash,    VGA13_WIDTH - 31, VGA13_HEIGHT - 42);

    
    for (int i = 0; i < desktop_icon_count; i++) {
        for (int j = 0; j < old_n; j++) {
            if (old[j].app == desktop_icons[i].app && old[j].app != APP_NONE) {
                desktop_icons[i].x = old[j].x;
                desktop_icons[i].y = old[j].y;
                break;
            }
        }
    }

    if (!fs_using_fat32()) { desktop_icons_dirty = 0; return; }

    fat32_dir_entry_t e;
    if (fat32_find_in_dir(fat32_get_root_cluster(), "DESKTOP", &e) != 0) {
        desktop_icons_dirty = 0; return;
    }
    desktop_folder_cluster = ((u32)e.cluster_high << 16) | (u32)e.cluster_low;
    if (desktop_folder_cluster < 2) { desktop_icons_dirty = 0; return; }

    FSDirEnt ents[FS_MAX_FILES];
    int n = fs_list_dir(desktop_folder_cluster, ents, FS_MAX_FILES);

    int gx = 50, gy = 22, col_w = 36, row_h = 40;
    int cols = (VGA13_WIDTH - 80) / col_w;
    if (cols < 1) cols = 1;
    int k = 0;
    for (int i = 0; i < n && desktop_icon_count < MAX_DESKTOP_ICONS; i++) {
        if (!ents[i].name[0]) continue;
        int x = gx + (k % cols) * col_w;
        int y = gy + (k / cols) * row_h;
        if (y > VGA13_HEIGHT - 60) break;
        desktop_add_icon_fs(ents[i].name, desktop_folder_cluster,
                            ents[i].first_cluster, ents[i].is_dir, x, y, ents[i].mtime);
        
        for (int j = 0; j < old_n; j++) {
            if (old[j].app == APP_NONE &&
                old[j].parent_dir_cluster == desktop_folder_cluster &&
                str_eq(old[j].label, ents[i].name)) {
                desktop_icons[desktop_icon_count - 1].x = old[j].x;
                desktop_icons[desktop_icon_count - 1].y = old[j].y;
                break;
            }
        }
        k++;
    }
    desktop_icons_dirty = 0;
}

static void desktop_ensure_icons_built(void) {
    if (desktop_icons_dirty) desktop_build_icons();
}

static int is_icon_selected(int idx) {
    int i;
    for (i = 0; i < num_selected_icons; i++)
        if (selected_icons[i] == idx) return 1;
    return 0;
}
static void add_to_selection(int idx) {
    if (idx >= 0 && idx < desktop_icon_count &&
        num_selected_icons < MAX_DESKTOP_ICONS && !is_icon_selected(idx))
        selected_icons[num_selected_icons++] = idx;
}
static void remove_from_selection(int idx) {
    int i, j;
    for (i = 0; i < num_selected_icons; i++) {
        if (selected_icons[i] == idx) {
            for (j = i; j < num_selected_icons - 1; j++)
                selected_icons[j] = selected_icons[j + 1];
            num_selected_icons--;
            return;
        }
    }
}
static void clear_selection(void) { num_selected_icons = 0; selected_icon = -1; }
static void toggle_selection(int idx) {
    if (is_icon_selected(idx)) remove_from_selection(idx);
    else                       add_to_selection(idx);
}
static void desktop_select_all_icons(void) {
    desktop_ensure_icons_built();
    num_selected_icons = 0; selected_icon = -1;
    for (int i = 0; i < desktop_icon_count && num_selected_icons < MAX_DESKTOP_ICONS; i++) {
        if (!is_icon_selected(i)) add_to_selection(i);
        if (selected_icon < 0) selected_icon = i;
    }
    desktop_needs_full_blit = 1;
}
static void desktop_relayout_by_name(void) {
    desktop_ensure_icons_built();
    int idx[MAX_DESKTOP_ICONS];
    for (int i = 0; i < desktop_icon_count; i++) idx[i] = i;
    for (int i = 0; i < desktop_icon_count; i++)
        for (int j = i + 1; j < desktop_icon_count; j++)
            if (str_cmp_simple(desktop_icons[idx[j]].label, desktop_icons[idx[i]].label) < 0) {
                int t = idx[i]; idx[i] = idx[j]; idx[j] = t;
            }
    int gx=50, gy=22, col_w=36, row_h=40;
    int cols = (VGA13_WIDTH - 80) / col_w; if (cols < 1) cols = 1;
    int k = 0;
    for (int ii = 0; ii < desktop_icon_count; ii++) {
        int i = idx[ii];
        desktop_icons[i].x = gx + (k % cols) * col_w;
        desktop_icons[i].y = gy + (k / cols) * row_h;
        if (desktop_icons[i].y > VGA13_HEIGHT - 60) break;
        k++;
    }
    desktop_needs_full_blit = 1;
}

static int desktop_icon_view_tier(const icon_t *ic) {
    if (ic->app != APP_NONE) return 0;
    return ic->is_dir ? 1 : 2;
}

static void desktop_relayout_by_icon(void) {
    desktop_ensure_icons_built();
    int idx[MAX_DESKTOP_ICONS];
    int i, j;
    for (i = 0; i < desktop_icon_count; i++) idx[i] = i;
    for (i = 0; i < desktop_icon_count; i++)
        for (j = i + 1; j < desktop_icon_count; j++) {
            int ai = idx[i], bi = idx[j];
            const icon_t *a = &desktop_icons[ai];
            const icon_t *b = &desktop_icons[bi];
            int ta = desktop_icon_view_tier(a);
            int tb = desktop_icon_view_tier(b);
            if (ta > tb || (ta == tb && str_cmp_simple(a->label, b->label) > 0)) {
                int t = idx[i]; idx[i] = idx[j]; idx[j] = t;
            }
        }
    {
        int gx = 50, gy = 22, col_w = 36, row_h = 40;
        int cols = (VGA13_WIDTH - 80) / col_w;
        int k = 0;
        if (cols < 1) cols = 1;
        for (i = 0; i < desktop_icon_count; i++) {
            int ii = idx[i];
            desktop_icons[ii].x = gx + (k % cols) * col_w;
            desktop_icons[ii].y = gy + (k / cols) * row_h;
            if (desktop_icons[ii].y > VGA13_HEIGHT - 60) break;
            k++;
        }
    }
    desktop_needs_full_blit = 1;
}

static void desktop_relayout_by_date(void) {
    desktop_ensure_icons_built();
    int idx[MAX_DESKTOP_ICONS];
    int i, j;
    for (i = 0; i < desktop_icon_count; i++) idx[i] = i;
    for (i = 0; i < desktop_icon_count; i++)
        for (j = i + 1; j < desktop_icon_count; j++) {
            int ai = idx[i], bi = idx[j];
            const icon_t *a = &desktop_icons[ai];
            const icon_t *b = &desktop_icons[bi];
            u32 ka = (a->app != APP_NONE) ? 0xFFFFFFFFu : a->mtime;
            u32 kb = (b->app != APP_NONE) ? 0xFFFFFFFFu : b->mtime;
            if (ka < kb || (ka == kb && str_cmp_simple(a->label, b->label) > 0)) {
                int t = idx[i]; idx[i] = idx[j]; idx[j] = t;
            }
        }
    {
        int gx = 50, gy = 22, col_w = 36, row_h = 40;
        int cols = (VGA13_WIDTH - 80) / col_w;
        int k = 0;
        if (cols < 1) cols = 1;
        for (i = 0; i < desktop_icon_count; i++) {
            int ii = idx[i];
            desktop_icons[ii].x = gx + (k % cols) * col_w;
            desktop_icons[ii].y = gy + (k / cols) * row_h;
            if (desktop_icons[ii].y > VGA13_HEIGHT - 60) break;
            k++;
        }
    }
    desktop_needs_full_blit = 1;
}

static void desktop_erase_root_files(void) {
    int i;
    fs_try_mount_fat32();
    if (!fs_using_fat32()) return;
    {
        FSDirEnt ents[FS_MAX_FILES];
        int n = fs_list_dir(fs_root_dir_cluster(), ents, FS_MAX_FILES);
        for (i = 0; i < n; i++) {
            if (ents[i].is_dir) continue;
            (void)fs_unlink(fs_root_dir_cluster(), ents[i].name);
        }
    }
    desktop_mark_icons_dirty();
    desktop_needs_full_blit = 1;
}

static void desktop_write_startup_for_icon(void) {
    char line[FS_MAX_NAME];
    icon_t *ic;
    if (selected_icon < 0 || selected_icon >= desktop_icon_count) return;
    fs_try_mount_fat32();
    if (!fs_using_fat32()) return;
    ic = &desktop_icons[selected_icon];
    line[0] = 0;
    if (ic->app != APP_NONE) {
        if (ic->app == APP_about) str_cpy(line, "ABOUT", sizeof(line));
        else if (ic->app == APP_notepad) str_cpy(line, "NOTEPAD", sizeof(line));
        else if (ic->app == APP_calc) str_cpy(line, "CALC", sizeof(line));
        else if (ic->app == APP_terminal) str_cpy(line, "TERMINAL", sizeof(line));
        else if (ic->app == APP_disk) str_cpy(line, "DISK", sizeof(line));
        else if (ic->app == APP_trash) str_cpy(line, "TRASH", sizeof(line));
        else if (ic->app == APP_control_panel) str_cpy(line, "CONTROLPANEL", sizeof(line));
        else if (ic->app == APP_puzzle) str_cpy(line, "PUZZLE", sizeof(line));
        else if (ic->app == APP_browser) str_cpy(line, "BROWSER", sizeof(line));
        else if (ic->app == APP_pong) str_cpy(line, "PONG", sizeof(line));
        else str_cpy(line, "ABOUT", sizeof(line));
    } else {
        str_cpy(line, ic->label, sizeof(line));
    }
    (void)fat32_write_file_new_in_dir(fs_root_dir_cluster(), "STARTAPP.TXT",
                                      line, (u32)str_len(line));
    desktop_mark_icons_dirty();
    desktop_needs_full_blit = 1;
}

static void desktop_clip_from_selected(int move) {
    desktop_ensure_icons_built();
    desktop_clip_valid = 0; desktop_clip_is_dir = 0;
    if (selected_icon < 0 || selected_icon >= desktop_icon_count) return;
    icon_t *ic = &desktop_icons[selected_icon];
    if (ic->app != APP_NONE || !ic->label[0]) return;
    desktop_clip_valid    = 1;
    desktop_clip_move     = move;
    desktop_clip_is_dir   = ic->is_dir;
    desktop_clip_src_dir  = ic->parent_dir_cluster;
    str_cpy(desktop_clip_name, ic->label, FS_MAX_NAME);
}
static void desktop_clip_paste_to_desktop(void) {
    if (!desktop_clip_valid) return;
    u32 dst = desktop_folder_cluster;
    if (dst < 2) dst = fs_root_dir_cluster();
    if (dst < 2) return;
    if (desktop_clip_is_dir) return;
    if (fs_using_fat32()) {
        (void)fs_copy_file(desktop_clip_src_dir, desktop_clip_name, dst, desktop_clip_name);
        if (desktop_clip_move && desktop_clip_src_dir != dst)
            (void)fs_unlink(desktop_clip_src_dir, desktop_clip_name);
    } else {
        if (desktop_clip_move) return;
        int ofd = fs_open(desktop_clip_name); if (ofd < 0) return;
        int nfd = fs_create(desktop_clip_name); if (nfd < 0) return;
        (void)fs_write(nfd, fs_get_data(ofd), fs_size(ofd));
    }
    desktop_mark_icons_dirty(); desktop_needs_full_blit = 1;
}
static void desktop_duplicate_selected_file(void) {
    desktop_ensure_icons_built();
    if (selected_icon < 0 || selected_icon >= desktop_icon_count) return;
    icon_t *ic = &desktop_icons[selected_icon];
    if (ic->app != APP_NONE || ic->is_dir || !ic->label[0]) return;
    u32 dst = desktop_folder_cluster;
    if (dst < 2) dst = fs_root_dir_cluster();
    if (dst < 2) return;
    char base[9]={0}, ext[4]={0}; int dot=-1;
    for (int i = 0; ic->label[i]; i++) if (ic->label[i] == '.') { dot=i; break; }
    if (dot >= 0) {
        int b = dot > 8 ? 8 : dot;
        for (int i = 0; i < b; i++) base[i] = ic->label[i];
        int e = 0;
        for (int i = dot+1; ic->label[i] && e < 3; i++) ext[e++] = ic->label[i];
    } else {
        int b = 0;
        while (ic->label[b] && b < 8) { base[b] = ic->label[b]; b++; }
    }
    for (int n = 1; n <= 99; n++) {
        char cb[9]={0}; char new_name[FS_MAX_NAME];
        int keep = str_len(base); if (keep > 5) keep = 5;
        for (int i = 0; i < keep; i++) cb[i] = base[i];
        int p = keep;
        if (p<8) cb[p++]='D'; if (p<8) cb[p++]='0'+(n/10); if (p<8) cb[p++]='0'+(n%10);
        if (ext[0]) ksnprintf(new_name, sizeof(new_name), "%s.%s", cb, ext);
        else        ksnprintf(new_name, sizeof(new_name), "%s", cb);
        if (fs_using_fat32()) {
            FSDirEnt ents[FS_MAX_FILES];
            int count = fs_list_dir(dst, ents, FS_MAX_FILES), exists=0;
            for (int i = 0; i < count; i++) if (str_eq(ents[i].name, new_name)) { exists=1; break; }
            if (exists) continue;
            (void)fs_copy_file(ic->parent_dir_cluster, ic->label, dst, new_name);
        } else {
            if (fs_exists(new_name)) continue;
            int ofd=fs_open(ic->label); if (ofd<0) return;
            int nfd=fs_create(new_name); if (nfd<0) return;
            (void)fs_write(nfd, fs_get_data(ofd), fs_size(ofd));
        }
        desktop_mark_icons_dirty(); desktop_needs_full_blit = 1; return;
    }
}
static int hit_icon(int mx, int my) {
    int i;
    desktop_ensure_icons_built();
    for (i = 0; i < desktop_icon_count; i++) {
        int x = desktop_icons[i].x, y = desktop_icons[i].y;
        if (mx>=x && mx<x+ICON_SIZE && my>=y && my<y+ICON_SIZE+ICON_LABEL_H+6) return i;
    }
    return -1;
}

static const char folder_icon_bmp[24][25] = {
    "........................",
    "........................",
    "........................",
    "........................",
    "........................",
    ".....BBBBB..............",
    "....BWWWWWB.............",
    "...BBBBBBBBBBBBBBBBBB...",
    "..BWWWWWWWWWWWWWWWWWWB..",
    "..BWWWWWWWWWWWWWWWWWWB..",
    "..BWWWWWWWWWWWWWWWWWWB..",
    "..BWWWWWWWWWWWWWWWWWWB..",
    "..BWWWWWWWWWWWWWWWWWWB..",
    "..BWWWWWWWWWWWWWWWWWWB..",
    "..BWWWWWWWWWWWWWWWWWWB..",
    "..BWWWWWWWWWWWWWWWWWWB..",
    "..BWWWWWWWWWWWWWWWWWWB..",
    "..BWWWWWWWWWWWWWWWWWWB..",
    "..BWWWWWWWWWWWWWWWWWWB..",
    "..BWWWWWWWWWWWWWWWWWWB..",
    "..BWWWWWWWWWWWWWWWWWWB..",
    "..BBBBBBBBBBBBBBBBBBBB..",
    "........................",
    "........................"
};
static const char file_icon_bmp[24][25] = {
    "........................",
    "...BBBBBBBBBBBBB........",
    "...BWWWWWWWWWWWBB.......",
    "...BWWWWWWWWWWWBWB......",
    "...BWWWWWWWWWWWBWWB.....",
    "...BWWWWWWWWWWWBWWWB....",
    "...BWWWWWWWWWWWBBBBBB...",
    "...BWWWWWWWWWWWWWWWWB...",
    "...BWWWWWWWWWWWWWWWWB...",
    "...BWWWWWWWWWWWWWWWWB...",
    "...BWWWWWWWWWWWWWWWWB...",
    "...BWWWWWWWWWWWWWWWWB...",
    "...BWWWWWWWWWWWWWWWWB...",
    "...BWWWWWWWWWWWWWWWWB...",
    "...BWWWWWWWWWWWWWWWWB...",
    "...BWWWWWWWWWWWWWWWWB...",
    "...BWWWWWWWWWWWWWWWWB...",
    "...BWWWWWWWWWWWWWWWWB...",
    "...BWWWWWWWWWWWWWWWWB...",
    "...BWWWWWWWWWWWWWWWWB...",
    "...BWWWWWWWWWWWWWWWWB...",
    "...BBBBBBBBBBBBBBBBBB...",
    "........................",
    "........................"
};

static void draw_bitmap_icon_24(int x, int y, const char bmp[24][25]) {
    int r, c;
    for (r = 0; r < 24; r++) {
        for (c = 0; c < 24; c++) {
            char ch = bmp[r][c];
            int px = x+c, py = y+r;
            if (px<0||px>=VGA13_WIDTH||py<0||py>=VGA13_HEIGHT) continue;
            switch (ch) {
                case 'W': vga13_put_pixel(px, py, VGA13_WHITE);      break;
                case 'B': vga13_put_pixel(px, py, VGA13_BLACK);      break;
                case 'G': vga13_put_pixel(px, py, PAL_LIGHT_GRAY);   break;
                case 'D': vga13_put_pixel(px, py, PAL_DARK_GRAY);    break;
                case 'O': vga13_put_pixel(px, py, 25);               break;
                case 'K': vga13_put_pixel(px, py, 26);               break;
                case 'L': vga13_put_pixel(px, py, 27);               break;
                case 'M': vga13_put_pixel(px, py, 28);               break;
                case 'N': vga13_put_pixel(px, py, 29);               break;
                case 'X': vga13_put_pixel(px, py, VGA13_BLACK);      break;
                default:  break;
            }
        }
    }
}

static void draw_icon_bitmap(int x, int y, const icon_t *ic) {
    const app_desc_t *desc;
    if (!ic) { vga13_fill_rect(x, y, ICON_SIZE, ICON_SIZE, PAL_DITHER_A); return; }
    if (ic->app == APP_NONE) {
        if (ic->is_dir) draw_bitmap_icon_24(x, y, folder_icon_bmp);
        else            draw_bitmap_icon_24(x, y, file_icon_bmp);
        return;
    }
    desc = app_registry_get(ic->app);
    if (desc && desc->icon_bmp) {
        draw_bitmap_icon_24(x, y, (const char (*)[25])desc->icon_bmp);
        return;
    }
    
    vga13_fill_rect(x, y, ICON_SIZE, ICON_SIZE, PAL_DITHER_A);
    vga13_fill_rect(x+6, y+6, 12, 12, PAL_LIGHT_GRAY);
}

static void draw_window_outline(int x, int y, int w, int h) {
    hline_px(x, x+w-1, y,     VGA13_BLACK);
    hline_px(x, x+w-1, y+h-1, VGA13_BLACK);
    vline_px(x,     y, y+h-1, VGA13_BLACK);
    vline_px(x+w-1, y, y+h-1, VGA13_BLACK);
}
static float ease_in(float t)  { return t * t; }
static float ease_out(float t) { float inv = 1.0f - t; return 1.0f - inv * inv; }

static void animate_window_zoom(int id) {
    int x, y, w, h; float t, e;
    if (id<0||id>=MAX_WIN||!wins[id].used||!wins[id].animating) return;
    t = (float)wins[id].anim_frame / (float)WIN_ANIM_FRAMES;
    if (wins[id].anim_dir == 1) {
        e = ease_in(t);
        x=(int)(wins[id].anim_sx+(wins[id].anim_ex-wins[id].anim_sx)*e);
        y=(int)(wins[id].anim_sy+(wins[id].anim_ey-wins[id].anim_sy)*e);
        w=(int)(wins[id].anim_sw+(wins[id].anim_ew-wins[id].anim_sw)*e);
        h=(int)(wins[id].anim_sh+(wins[id].anim_eh-wins[id].anim_sh)*e);
    } else {
        e = ease_out(1.0f - t);
        x=(int)(wins[id].anim_ex+(wins[id].anim_sx-wins[id].anim_ex)*e);
        y=(int)(wins[id].anim_ey+(wins[id].anim_sy-wins[id].anim_ey)*e);
        w=(int)(wins[id].anim_ew+(wins[id].anim_sw-wins[id].anim_ew)*e);
        h=(int)(wins[id].anim_eh+(wins[id].anim_sh-wins[id].anim_eh)*e);
    }
    if (w<4) w=4; if (h<4) h=4;
    draw_window_outline(x, y, w, h);
    wins[id].anim_frame++;
    if (wins[id].anim_frame > WIN_ANIM_FRAMES) {
        wins[id].animating = 0;
        desktop_needs_full_blit = 1;
        if (wins[id].anim_dir == -1) {
            wins[id].used = 0;
            if (drag_id == id) drag_id = -1;
        }
    }
}
static int any_window_animating(void) {
    int i;
    for (i = 0; i < MAX_WIN; i++)
        if (wins[i].used && wins[i].animating) return 1;
    return 0;
}
static void draw_animating_windows(void) {
    int i;
    for (i = 0; i < MAX_WIN; i++)
        if (wins[i].used && wins[i].animating) {
            animate_window_zoom(i); desktop_needs_full_blit = 1;
        }
}

static void draw_client(int id) {
    int cx, cy, cw, ch;
    const app_desc_t *desc;
    if (id<0||!wins[id].used) return;
    cx = wins[id].x + 3;
    cy = wins[id].y + SVS_TITLE_H + 1;
    cw = wins[id].w - 6;
    ch = wins[id].h - SVS_TITLE_H - 3;
    if (cw<8||ch<8) return;
    desc = app_registry_get(wins[id].app);
    if (desc && desc->draw) desc->draw(id, cx, cy, cw, ch);
    else                    vga13_fill_rect(cx, cy, cw, ch, VGA13_WHITE);
}

static void draw_one_window(int id) {
    if (id<0||!wins[id].used) return;
    draw_svs_window(wins[id].x, wins[id].y, wins[id].w, wins[id].h,
                    wins[id].title, id == win_top_id());
    draw_client(id);
}
static void draw_windows_bottom_to_top(void) {
    int zi, i, zm = win_max_z();
    if (zm<1) zm=1;
    for (zi=1; zi<=zm; zi++)
        for (i=0; i<MAX_WIN; i++)
            if (wins[i].used && wins[i].z==zi && !wins[i].animating) draw_one_window(i);
}

static void draw_desktop_background(void) {
    int x, y, bg_y0 = SVS_MENU_BAR_H + 1;
    u8 base = PAL_DESKTOP;
    u8 dith = PAL_DITHER_A;

    switch (ui_desktop_color) {
    case 0: base = VGA13_BLACK;    dith = PAL_DARK_GRAY;   break;
    case 1: base = VGA13_BLACK;    dith = PAL_DITHER_A;    break;
    case 2: base = PAL_DARK_GRAY;  dith = PAL_DITHER_A;    break;
    case 3: base = PAL_DARK_GRAY;  dith = PAL_LIGHT_GRAY;  break;
    case 4: base = PAL_DITHER_A;   dith = PAL_LIGHT_GRAY;  break;
    case 5: base = PAL_LIGHT_GRAY; dith = PAL_DESKTOP;     break;
    case 6: base = VGA13_WHITE;    dith = PAL_LIGHT_GRAY;  break;
    case 7: base = PAL_DESKTOP;    dith = VGA13_WHITE;     break;
    default: break;
    }

if (ui_desktop_pattern == 1) {
    for (y = bg_y0; y < VGA13_HEIGHT; y++)
        for (x = 0; x < VGA13_WIDTH; x++)
            vga13_put_pixel(x, y, ((y % 4 == 0 && x % 4 == 0) ||
                                   (y % 4 == 2 && x % 4 == 2)) ? base : dith);
    return;
}
    if (ui_desktop_pattern == 2) {
        
        for (y = bg_y0; y < VGA13_HEIGHT; y++)
            for (x = 0; x < VGA13_WIDTH; x++)
                vga13_put_pixel(x, y, (((x+y)&1)==0) ? base : dith);
        return;
    }

    
    vga13_fill_rect(0, bg_y0, VGA13_WIDTH, VGA13_HEIGHT - bg_y0, base);
    for (y = bg_y0; y < VGA13_HEIGHT; y++)
        for (x = 0; x < VGA13_WIDTH; x++)
            if (((x^y)&1)==0) vga13_put_pixel(x, y, dith);
}

static void draw_desktop_icons(void) {
    int i;
    desktop_ensure_icons_built();
    for (i=0; i<desktop_icon_count; i++) {
        int ix = desktop_icons[i].x, iy = desktop_icons[i].y;
        int ll = str_len(desktop_icons[i].label);
        int lx = ix + (ICON_SIZE - ll*6) / 2;
        int is_sel = is_icon_selected(i) || selected_icon==i;
        draw_icon_bitmap(ix, iy, &desktop_icons[i]);
        if (is_sel) {
            int yy, xx;
            for (yy=iy; yy<iy+ICON_SIZE; yy++)
                for (xx=ix; xx<ix+ICON_SIZE; xx++)
                    if (((xx+yy)&1)==0) vga13_put_pixel(xx, yy, PAL_DARK_GRAY);
            vga13_fill_rect(lx-1, iy+ICON_SIZE+1, ll*6+2, ICON_LABEL_H, VGA13_BLACK);
        } else {
            vga13_fill_rect(lx-1, iy+ICON_SIZE+1, ll*6+2, ICON_LABEL_H, VGA13_WHITE);
        }
        vga13_draw_string(lx, iy+ICON_SIZE+2, desktop_icons[i].label, VGA13_BLACK, VGA13_WHITE, is_sel);
    }
}

static void draw_menu_bar_interactive(void) {
    int i;
    draw_global_menu_bar(0);
    for (i=0; i<MENU_COUNT; i++) {
        int inv = ((menu_open==i)||(menu_open<0 && menu_hot==i));
        if (inv) vga13_fill_rect(menus[i].x, 1, menus[i].w, SVS_MENU_BAR_H-2, VGA13_BLACK);
        if (i==0) {
            u8 c = inv ? VGA13_WHITE : VGA13_BLACK;
            hline_px(5,11,3,c); hline_px(5,11,9,c);
            vline_px(5,3,9,c);  vline_px(11,3,9,c);
        } else {
            vga13_draw_string(menus[i].x+5, 3, menus[i].title, VGA13_BLACK, VGA13_WHITE, inv);
        }
    }
    {
        char clk[16];
        extern int ui_clock_show_seconds;
        rtc_get_time_string(clk, sizeof(clk), ui_clock_show_seconds, ui_clock_12hr);
        int clk_w = str_len(clk)*6;
        int clk_x = VGA13_WIDTH - clk_w - 6;
        if (clk_x < 140) clk_x = 140;
        vga13_draw_string(clk_x, 3, clk, VGA13_BLACK, VGA13_WHITE, 0);
    }
}

static void draw_dropdown_topmost(void) {
    int i, x, y, w, h;
    if (menu_open<0) return;
    x = menus[menu_open].x;
    y = SVS_MENU_BAR_H;
    w = dropdown_w(menu_open);
    h = menus[menu_open].item_count * 11 + 6;
    vga13_fill_rect(x+1, y+h, w, 1, PAL_DARK_GRAY);
    vga13_fill_rect(x+w, y+1, 1, h, PAL_DARK_GRAY);
    vga13_fill_rect(x, y, w, h, VGA13_WHITE);
    hline_px(x, x+w-1, y,     VGA13_BLACK);
    hline_px(x, x+w-1, y+h-1, VGA13_BLACK);
    vline_px(x,     y, y+h-1, VGA13_BLACK);
    vline_px(x+w-1, y, y+h-1, VGA13_BLACK);
    for (i=0; i<menus[menu_open].item_count; i++) {
        int iy  = y + 2 + i*11;
        int inv = (i == menu_hover);
        if (inv) vga13_fill_rect(x+2, iy-1, w-4, 10, VGA13_BLACK);
        vga13_draw_string(x+8, iy, menus[menu_open].items[i].label, VGA13_BLACK, VGA13_WHITE, inv);
    }
}

static void draw_lasso(void) {
    int x0, y0, x1, y1, i;
    if (!lasso_active) return;
    x0 = lasso_start_x < lasso_cur_x ? lasso_start_x : lasso_cur_x;
    y0 = lasso_start_y < lasso_cur_y ? lasso_start_y : lasso_cur_y;
    x1 = lasso_start_x > lasso_cur_x ? lasso_start_x : lasso_cur_x;
    y1 = lasso_start_y > lasso_cur_y ? lasso_start_y : lasso_cur_y;
    for (i=x0; i<=x1; i+=2) { vga13_put_pixel(i,y0,VGA13_BLACK); vga13_put_pixel(i,y1,VGA13_BLACK); }
    for (i=y0; i<=y1; i+=2) { vga13_put_pixel(x0,i,VGA13_BLACK); vga13_put_pixel(x1,i,VGA13_BLACK); }
}

static int ctx_menu_width(void) {
    int i, w=70;
    for (i=0; i<CTX_MENU_ITEMS; i++) {
        int tw = str_len(ctx_menu_labels[i])*6+16;
        if (tw>w) w=tw;
    }
    return w;
}
static void draw_context_menu(void) {
    int i, w, h;
    if (!ctx_menu_open) return;
    w = ctx_menu_width(); h = CTX_MENU_ITEMS*11+6;
    if (ctx_menu_x+w > VGA13_WIDTH)  ctx_menu_x = VGA13_WIDTH-w;
    if (ctx_menu_y+h > VGA13_HEIGHT) ctx_menu_y = VGA13_HEIGHT-h;
    vga13_fill_rect(ctx_menu_x+1, ctx_menu_y+h, w, 1, PAL_DARK_GRAY);
    vga13_fill_rect(ctx_menu_x+w, ctx_menu_y+1, 1, h, PAL_DARK_GRAY);
    vga13_fill_rect(ctx_menu_x, ctx_menu_y, w, h, VGA13_WHITE);
    hline_px(ctx_menu_x, ctx_menu_x+w-1, ctx_menu_y,     VGA13_BLACK);
    hline_px(ctx_menu_x, ctx_menu_x+w-1, ctx_menu_y+h-1, VGA13_BLACK);
    vline_px(ctx_menu_x,     ctx_menu_y, ctx_menu_y+h-1, VGA13_BLACK);
    vline_px(ctx_menu_x+w-1, ctx_menu_y, ctx_menu_y+h-1, VGA13_BLACK);
    for (i=0; i<CTX_MENU_ITEMS; i++) {
        int iy = ctx_menu_y+2+i*11;
        int inv = (i==ctx_menu_hover);
        if (inv) vga13_fill_rect(ctx_menu_x+2, iy-1, w-4, 10, VGA13_BLACK);
        vga13_draw_string(ctx_menu_x+8, iy, ctx_menu_labels[i], VGA13_BLACK, VGA13_WHITE, inv);
    }
}
static int hit_context_menu(int mx, int my) {
    int w = ctx_menu_width(), h = CTX_MENU_ITEMS*11+6;
    if (!ctx_menu_open) return -1;
    if (mx<ctx_menu_x||mx>=ctx_menu_x+w||my<ctx_menu_y||my>=ctx_menu_y+h) return -1;
    return (my-ctx_menu_y-3)/11;
}

static void wm_draw_all(void) {
    int i;
    vga13_clear_back_buffer();
    draw_desktop_background();
    draw_desktop_icons();
    draw_lasso();
    draw_windows_bottom_to_top();
    draw_animating_windows();
    draw_menu_bar_interactive();
    draw_dropdown_topmost();
    draw_context_menu();

    
    for (i=0; i<MAX_WIN; i++) {
        if (wins[i].used) {
            const app_desc_t *desc = app_registry_get(wins[i].app);
            if (desc && desc->draw_overlay) desc->draw_overlay();
        }
    }

    mouse_cursor_draw_to_buffer();
    vga13_flip_buffer();
}

static int hit_close(int id, int mx, int my) {
    int bx, by;
    if (id<0||!wins[id].used) return 0;
    bx = wins[id].x+4; by = wins[id].y+2;
    return mx>=bx && mx<bx+SVS_CTRL_SIZE && my>=by && my<by+SVS_CTRL_SIZE;
}
static int hit_title_drag(int id, int mx, int my) {
    int tx0, tx1;
    if (id<0||!wins[id].used) return 0;
    if (my<wins[id].y+1||my>=wins[id].y+SVS_TITLE_H) return 0;
    tx0 = wins[id].x+2;
    tx1 = wins[id].x+wins[id].w-SVS_CTRL_SIZE-7;
    return mx>=tx0 && mx<tx1;
}

static void context_menu_click(int mx, int my) {
    int item = hit_context_menu(mx, my);
    if (item < 0 || item >= CTX_MENU_ITEMS) { ctx_menu_open=0; ctx_menu_hover=-1; desktop_needs_full_blit=1; return; }

    if (item == 0) {  
        fs_try_mount_fat32();
        u32 tgt = desktop_folder_cluster;
        if (tgt<2 && fs_using_fat32()) tgt = fs_root_dir_cluster();
        if (tgt>=2) {
            char nm[FS_MAX_NAME]; int n=1; nm[0]=0;
            for (;;) {
                char tmp[FS_MAX_NAME];
                tmp[0]='N';tmp[1]='E';tmp[2]='W';tmp[3]='F';tmp[4]='O';tmp[5]='L';tmp[6]='D';
                tmp[7]=(char)('0'+(n%10));tmp[8]=0;
                FSDirEnt ents[FS_MAX_FILES];
                int count=fs_list_dir(tgt,ents,FS_MAX_FILES), exists=0;
                for (int i=0;i<count;i++) if (str_eq(ents[i].name,tmp)){exists=1;break;}
                if (!exists){str_cpy(nm,tmp,FS_MAX_NAME);break;}
                n++; if (n>9){str_cpy(nm,"NEWFOLD9",FS_MAX_NAME);break;}
            }
            (void)fs_mkdir(tgt, nm);
            desktop_mark_icons_dirty();
        }
    } else if (item == 1) { 
        if (ctx_menu_target_icon >= 0) {
            icon_t *ic = &desktop_icons[ctx_menu_target_icon];
            if (ic->app != APP_NONE) app_open(ic->app);
            else if (ic->is_dir)    open_disk_at_cluster(ic->first_cluster, ic->label);
            else                    notepad_open_file(ic->parent_dir_cluster, ic->label);
        }
    } else if (item == 2) { 
        win_open(APP_about, "Get Info", 60, 40, 210, 110);
    } else if (item == 3) { 
        
        if (ctx_menu_target_icon >= 0) {
            icon_t *ic = &desktop_icons[ctx_menu_target_icon];
            if (ic->app==APP_NONE && !ic->is_dir && ic->label[0]) {
                u32 src = ic->parent_dir_cluster;
                if (src<2) src=fs_root_dir_cluster();
                char base[9]={0}, ext[4]={0}; int dot=-1;
                for (int i=0;ic->label[i];i++) if (ic->label[i]=='.'){dot=i;break;}
                if (dot>=0){
                    int b=dot>8?8:dot;
                    for (int i=0;i<b;i++) base[i]=ic->label[i];
                    int e=0; for (int i=dot+1;ic->label[i]&&e<3;i++) ext[e++]=ic->label[i];
                } else { int b=0; while(ic->label[b]&&b<8){base[b]=ic->label[b];b++;} }
                char new_name[FS_MAX_NAME]; new_name[0]=0;
                for (int n=1;n<=99;n++){
                    char cb[9]={0};
                    int keep=str_len(base); if(keep>5)keep=5;
                    for(int i=0;i<keep;i++) cb[i]=base[i];
                    int p=keep;
                    if(p<8)cb[p++]='R'; if(p<8)cb[p++]='0'+(n/10); if(p<8)cb[p++]='0'+(n%10);
                    if(ext[0]) ksnprintf(new_name,sizeof(new_name),"%s.%s",cb,ext);
                    else       ksnprintf(new_name,sizeof(new_name),"%s",cb);
                    if(fs_using_fat32()){
                        FSDirEnt ents[FS_MAX_FILES];
                        int count=fs_list_dir(src,ents,FS_MAX_FILES),exists=0;
                        for(int i=0;i<count;i++) if(str_eq(ents[i].name,new_name)){exists=1;break;}
                        if(exists) continue;
                        
                        if(fs_copy_file(src,ic->label,src,new_name)==0){
                            (void)fs_unlink(src,ic->label);
                            desktop_mark_icons_dirty();break;
                        }
                    } else { break; }
                }
            }
        }
    } else if (item == 4) { 
    if (ctx_menu_target_icon >= 0) {
        icon_t *ic = &desktop_icons[ctx_menu_target_icon];
        if (ic->app == APP_NONE && !ic->is_dir) {
            trash_move_file(ic->parent_dir_cluster, ic->label);
            desktop_mark_icons_dirty();
        }
    }
} else if (item == 5) { 
        if (ctx_menu_target_icon >= 0) {
            icon_t *ic = &desktop_icons[ctx_menu_target_icon];
            if (ic->app == APP_disk) {
                for (int i=0;i<MAX_WIN;i++)
                    if (wins[i].used && wins[i].app==APP_disk) win_close(i);
                desktop_needs_full_blit = 1;
            }
        }
    }
    ctx_menu_open=0; ctx_menu_hover=-1; desktop_needs_full_blit=1;
}

static void handle_menu_action(app_kind_t item_app, const char *label) {
    int active = win_top_id();

    
    if (active >= 0) {
        const app_desc_t *desc = app_registry_get(wins[active].app);
        if (desc && desc->on_menu_action && desc->on_menu_action(active, label)) {
            desktop_needs_full_blit = 1;
            return;
        }
    }

    
    if (item_app != APP_NONE) {
        app_open(item_app);
        return;
    }

    
    if (str_eq(label, "Shut Down...") || str_eq(label, "Shut Down")) {
        __asm__ volatile ("cli; hlt");
    } else if (str_eq(label, "New Folder")) {
        fs_try_mount_fat32();
        u32 tgt = desktop_folder_cluster;
        if (tgt<2 && fs_using_fat32()) tgt=fs_root_dir_cluster();
        if (tgt>=2){
            char nm[FS_MAX_NAME]; int n=1; nm[0]=0;
            for(;;){
                char tmp[FS_MAX_NAME];
                FSDirEnt ents[FS_MAX_FILES];
                int count,exists=0;
                tmp[0]='N';tmp[1]='E';tmp[2]='W';tmp[3]='F';tmp[4]='O';tmp[5]='L';tmp[6]='D';
                tmp[7]=(char)('0'+(n%10));tmp[8]=0;
                count=fs_list_dir(tgt,ents,FS_MAX_FILES);
                for(int i=0;i<count;i++) if(str_eq(ents[i].name,tmp)){exists=1;break;}
                if(!exists){str_cpy(nm,tmp,FS_MAX_NAME);break;}
                n++; if(n>9){str_cpy(nm,"NEWFOLD9",FS_MAX_NAME);break;}
            }
            (void)fs_mkdir(tgt,nm);
            desktop_mark_icons_dirty(); desktop_needs_full_blit=1;
        }
    } else if (str_eq(label, "Open")) {
        if (selected_icon>=0){
            icon_t *ic=&desktop_icons[selected_icon];
            if (ic->app!=APP_NONE) app_open(ic->app);
            else if (ic->is_dir)   open_disk_at_cluster(ic->first_cluster,ic->label);
            else                   notepad_open_file(ic->parent_dir_cluster,ic->label);
        }
    } else if (str_eq(label, "Close")) {
        if (active>=0) win_close(active);
    } else if (str_eq(label, "Get Info")) {
        win_open(APP_about, "Get Info", 60, 40, 210, 110);
    } else if (str_eq(label, "Duplicate"))  { desktop_duplicate_selected_file(); }
    else if (str_eq(label, "Cut"))          { desktop_clip_from_selected(1); }
    else if (str_eq(label, "Copy"))         { desktop_clip_from_selected(0); }
    else if (str_eq(label, "Paste"))        { desktop_clip_paste_to_desktop(); }
    else if (str_eq(label, "Select All"))   { desktop_select_all_icons(); }
    else if (str_eq(label, "By Name"))      { desktop_relayout_by_name(); }
    else if (str_eq(label, "By Icon"))       { desktop_relayout_by_icon(); }
    else if (str_eq(label, "By Date"))       { desktop_relayout_by_date(); }
    else if (str_eq(label, "Clean Up"))     { desktop_relayout_by_name(); }
    else if (str_eq(label, "Eject Disk"))   {
        for (int i=0;i<MAX_WIN;i++)
            if (wins[i].used && wins[i].app==APP_disk) win_close(i);
        desktop_needs_full_blit=1;
    } else if (str_eq(label, "Erase Disk")) {
        desktop_erase_root_files();
    } else if (str_eq(label, "Set Startup")) {
        desktop_write_startup_for_icon();
    }
}

static void process_drag(int mx, int my) {
    int ox, oy;
    if (drag_id<0||!(mouse_buttons&1)||menu_open>=0) return;
    ox=wins[drag_id].x; oy=wins[drag_id].y;
    wins[drag_id].x = mx-drag_off_x;
    wins[drag_id].y = my-drag_off_y;
    win_clamp(drag_id);
    if (wins[drag_id].x!=ox||wins[drag_id].y!=oy) desktop_needs_full_blit=1;
}
static void process_icon_drag(int mx, int my) {
    int idx=icon_drag_idx;
    if (!icon_dragging||idx<0||idx>=desktop_icon_count) return;
    if (!(mouse_buttons&1)){icon_dragging=0;icon_drag_idx=-1;return;}
    desktop_icons[idx].x = mx-icon_drag_off_x;
    desktop_icons[idx].y = my-icon_drag_off_y;
    desktop_needs_full_blit=1;
}

static void desktop_manager_click(int mx, int my, int left_edge) {
    int wid, ic, mtitle, mitem; u32 t;
    if (!left_edge) return;

    mtitle = hit_menu_title(mx, my);
    if (mtitle >= 0) {
        menu_open=mtitle; menu_hover=-1; clear_selection(); return;
    }

    if (menu_open >= 0) {
        mitem = hit_menu_item(mx, my);
        if (mitem >= 0) {
            app_kind_t item_app = menus[menu_open].items[mitem].app;
            const char *label   = menus[menu_open].items[mitem].label;
            handle_menu_action(item_app, label);
        }
        menu_open=-1; menu_hover=-1; return;
    }

    wid = hit_top_window(mx, my);
    if (wid >= 0) {
        bring_to_front(wid);
        if (hit_close(wid, mx, my)) { win_close(wid); return; }
        if (hit_title_drag(wid, mx, my)) {
            drag_id=wid; drag_off_x=mx-wins[wid].x; drag_off_y=my-wins[wid].y;
        }
        return;
    }

    ic = hit_icon(mx, my);
    if (ic >= 0) {
        int dx, dy;
        t = timer_ticks();
        if (shift_held)            toggle_selection(ic);
        else if (is_icon_selected(ic)) {  }
        else                       { clear_selection(); add_to_selection(ic); }
        selected_icon = ic;

        dx=mx-last_click_x; dy=my-last_click_y;
        if (last_click_icon==ic && (t-last_click_ticks)<=DBL_CLICK_TICKS &&
            dx>=-DBL_CLICK_DIST && dx<=DBL_CLICK_DIST &&
            dy>=-DBL_CLICK_DIST && dy<=DBL_CLICK_DIST) {
            if (desktop_icons[ic].app!=APP_NONE) app_open(desktop_icons[ic].app);
            else if (desktop_icons[ic].is_dir) open_disk_at_cluster(desktop_icons[ic].first_cluster, desktop_icons[ic].label);
            else notepad_open_file(desktop_icons[ic].parent_dir_cluster, desktop_icons[ic].label);
        } else {
            icon_drag_idx=ic;
            icon_drag_off_x=mx-desktop_icons[ic].x;
            icon_drag_off_y=my-desktop_icons[ic].y;
            icon_dragging=1;
        }
        last_click_icon=ic; last_click_ticks=t; last_click_x=mx; last_click_y=my;
        return;
    }

    if (!shift_held) clear_selection();
    lasso_active=1; lasso_start_x=mx; lasso_start_y=my; lasso_cur_x=mx; lasso_cur_y=my;
}

static void frame_loop(void) {
    int mx=(int)mouse_x, my=(int)mouse_y;
    static int prev_mx=-1, prev_my=-1;
    u8 b=mouse_buttons;
    u8 left_edge  = (u8)((b&1)&&!(prev_buttons&1));
    u8 left_up    = (u8)(!(b&1)&&(prev_buttons&1));
    u8 right_edge = (u8)((b&2)&&!(prev_buttons&2));
    int old_hot=menu_hot, old_hover=menu_hover, old_open=menu_open;
    int old_sel=selected_icon, old_ctx_hover=ctx_menu_hover;
    int click_consumed=0, any_animating=0, i;

    
    {
        int active = win_top_id();
        switch_menus_for_app(active >= 0 ? wins[active].app : APP_NONE);
    }

    
    if (ctx_menu_open) {
        ctx_menu_hover = hit_context_menu(mx, my);
        if (left_edge) {
            context_menu_click(mx, my);
            prev_buttons=b; desktop_needs_full_blit=1; return;
        }
        if (left_up && hit_context_menu(mx,my)<0) {
            ctx_menu_open=0; ctx_menu_hover=-1; desktop_needs_full_blit=1;
        }
    }

    
    for (i=0; i<MAX_WIN; i++) {
        const app_desc_t *desc;
        if (!wins[i].used) continue;
        desc = app_registry_get(wins[i].app);
        if (!desc || !desc->overlay_is_open || !desc->overlay_is_open()) continue;

        if (desc->hover_overlay) desc->hover_overlay(mx, my);

        if (left_edge && desc->hit_overlay && desc->hit_overlay(mx,my)>=0) {
            if (desc->click_overlay) desc->click_overlay(mx, my, i);
            prev_buttons=b; desktop_needs_full_blit=1; goto frame_end;
        }
        if (left_up && desc->hit_overlay && desc->hit_overlay(mx,my)<0) {
            if (desc->close_overlay) desc->close_overlay();
            desktop_needs_full_blit=1;
        }
    }

    
    if (right_edge && menu_open>=0) { menu_open=-1; menu_hover=-1; }

    
    if (right_edge && !ctx_menu_open && !app_registry_any_overlay_open()) {
        int wid = hit_top_window(mx, my);
        if (wid >= 0) {
            const app_desc_t *desc = app_registry_get(wins[wid].app);
            if (desc && desc->on_right_click) {
                int cx=wins[wid].x+3, cy=wins[wid].y+SVS_TITLE_H+1;
                int cw=wins[wid].w-6, ch=wins[wid].h-SVS_TITLE_H-3;
                if (mx>=cx && mx<cx+cw && my>=cy && my<cy+ch) {
                    app_mouse_t ev = {mx,my,cx,cy,cw,ch,2};
                    desc->on_right_click(wid, &ev);
                    prev_buttons=b; desktop_needs_full_blit=1; return;
                }
            }
        } else {
            
            ctx_menu_x=mx; ctx_menu_y=my; ctx_menu_open=1;
            ctx_menu_hover=-1; ctx_menu_target_icon=hit_icon(mx,my);
            desktop_needs_full_blit=1; prev_buttons=b; return;
        }
    }

    
    if (left_up) {
        drag_id=-1;
        if (lasso_active) {
            int x0 = lasso_start_x<lasso_cur_x?lasso_start_x:lasso_cur_x;
            int y0 = lasso_start_y<lasso_cur_y?lasso_start_y:lasso_cur_y;
            int x1 = lasso_start_x>lasso_cur_x?lasso_start_x:lasso_cur_x;
            int y1 = lasso_start_y>lasso_cur_y?lasso_start_y:lasso_cur_y;
            desktop_ensure_icons_built();
            for (i=0; i<desktop_icon_count; i++) {
                int ix=desktop_icons[i].x, iy=desktop_icons[i].y;
                if (ix+ICON_SIZE>=x0&&ix<=x1&&iy+ICON_SIZE>=y0&&iy<=y1)
                    if (!is_icon_selected(i)) add_to_selection(i);
            }
            lasso_active=0;
        }
        icon_dragging=0; icon_drag_idx=-1;

        
        {
            int active=win_top_id();
            if (active>=0) {
                const app_desc_t *desc=app_registry_get(wins[active].app);
                if (desc && desc->on_release) {
                    int cx=wins[active].x+3, cy=wins[active].y+SVS_TITLE_H+1;
                    int cw=wins[active].w-6, ch=wins[active].h-SVS_TITLE_H-3;
                    app_mouse_t ev={mx,my,cx,cy,cw,ch,0};
                    desc->on_release(active,&ev);
                }
            }
        }
    }

    if (lasso_active && (b&1)) { lasso_cur_x=mx; lasso_cur_y=my; desktop_needs_full_blit=1; }
    menu_hot   = hit_menu_title(mx, my);
    menu_hover = hit_menu_item(mx, my);

    click_consumed = (left_edge && menu_open>=0 && hit_menu_item(mx,my)>=0);

    process_icon_drag(mx, my);
    desktop_manager_click(mx, my, left_edge);

    
    if (left_edge && !click_consumed) {
        int active=win_top_id();
        if (active>=0 && menu_open<0) {
            const app_desc_t *desc=app_registry_get(wins[active].app);
            if (desc && desc->on_click) {
                int cx=wins[active].x+3, cy=wins[active].y+SVS_TITLE_H+1;
                int cw=wins[active].w-6, ch=wins[active].h-SVS_TITLE_H-3;
                if (mx>=cx && mx<cx+cw && my>=cy && my<cy+ch) {
                    app_mouse_t ev={mx,my,cx,cy,cw,ch,0};
                    desc->on_click(active,&ev);
                }
            }
        }
    }

    
    if ((b&1) && !left_edge) {
        int active=win_top_id();
        if (active>=0) {
            const app_desc_t *desc=app_registry_get(wins[active].app);
            if (desc && desc->on_drag) {
                int cx=wins[active].x+3, cy=wins[active].y+SVS_TITLE_H+1;
                int cw=wins[active].w-6, ch=wins[active].h-SVS_TITLE_H-3;
                app_mouse_t ev={mx,my,cx,cy,cw,ch,0};
                desc->on_drag(active,&ev);
            }
        }
    }

    process_drag(mx, my);
    prev_buttons=b;

    for (i=0;i<MAX_WIN;i++) if (wins[i].used&&wins[i].animating){any_animating=1;break;}
    if (left_edge||left_up||lasso_active||icon_dragging||any_animating||
        old_hot!=menu_hot||old_hover!=menu_hover||old_open!=menu_open||
        old_sel!=selected_icon||old_ctx_hover!=ctx_menu_hover||
        mx!=prev_mx||my!=prev_my)
        desktop_needs_full_blit=1;

    prev_mx=mx; prev_my=my;
frame_end:;
}

void savaos_desktop_run(void) {
    int i;
    for (i=0;i<MAX_WIN;i++){wins[i].used=0;wins[i].animating=0;}
    z_seq=0; drag_id=-1; prev_buttons=0;
    selected_icon=-1; last_click_icon=-1; num_selected_icons=0;
    shift_held=0; icon_dragging=0; icon_drag_idx=-1;
    lasso_active=0; menu_open=-1; menu_hover=-1; menu_hot=-1;
    ctx_menu_open=0; ctx_menu_hover=-1; ctx_menu_target_icon=-1;
    current_menu_app=APP_NONE;
    current_menus=desk_menus; current_menu_count=DESK_MENU_COUNT; menus=desk_menus;

    
    app_registry_init_all();

    vga13_init();
    vga13_init_palette_sv();
    vga13_clear_vram();
    mouse_init();
    kb_set_irq_mode(1);
    layout_menus();

    win_open(APP_about,   "Welcome",    40, 30, 210, 110); 

    fs_try_mount_fat32();
    desktop_mark_icons_dirty();
    desktop_needs_full_blit=1;

    
static u32 cursor_last_phase = 0;

for (;;) {
    timer_poll();
    mouse_poll_packets();
    frame_loop();

if (kb_available()) {
    int c = kb_poll();
    if (c == KEY_ESC) {
        if (ctx_menu_open) {
            ctx_menu_open=0; ctx_menu_hover=-1;
        } else if (app_registry_any_overlay_open()) {
            for (i=0;i<MAX_WIN;i++) {
                if (!wins[i].used) continue;
                const app_desc_t *desc=app_registry_get(wins[i].app);
                if (desc && desc->overlay_is_open && desc->overlay_is_open() && desc->close_overlay)
                    desc->close_overlay();
            }
        } else {
            menu_open=-1;
        }
    } else {
        int active=win_top_id();
        if (active>=0) {
            const app_desc_t *desc=app_registry_get(wins[active].app);
            if (desc && desc->on_key) desc->on_key(active, c);
        }
    }
    desktop_needs_full_blit=1;
}

{
    u32 phase = (u32)(ui_caret_blink_fast ? 40u : 120u);
    u32 cur_phase = (u32)timer_ticks() / phase;
    if (cur_phase != cursor_last_phase) {
        cursor_last_phase = cur_phase;
        desktop_needs_full_blit = 1;
    }
}

    if (desktop_needs_full_blit) {
        int was_animating = any_window_animating();
        desktop_needs_full_blit = 0;
        wm_draw_all();
        if (any_window_animating()) {
            sleep_ms(WIN_ANIM_DELAY_MS);
            desktop_needs_full_blit = 1;
        } else if (was_animating) {
            wm_draw_all();
        }
    }
}
}
