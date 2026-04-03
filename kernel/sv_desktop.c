#include "types.h"
#include "sv_gfx.h"
#include "sv_desktop.h"
#include "terminal.h"
#include "mouse.h"
#include "keyboard.h"
#include "timer.h"
#include "rtc.h"
#include "string.h"
#include "fs.h"
#include "fat32.h"
#include "ata.h"
#include "irq.h"


static void hline_px(int x1, int x2, int y, u8 c);
static void vline_px(int x, int y1, int y2, u8 c);
static int hit_notepad_context_menu(int mx, int my);
static void notepad_context_menu_click(int mx, int my, int id);
static int win_top_id(void);
static void bring_to_front(int win_idx);
static void handle_disk_key(int id, int key);
int ksnprintf(char *buf, int cap, const char *fmt, ...);
static void handle_puzzle_key(int id, int key);
static void puzzle_init(int id);

#define MAX_WIN 8
#define DESKTOP_Y 20
#define DESKTOP_H (VGA13_HEIGHT - DESKTOP_Y)
#define ICON_SIZE 24
#define ICON_LABEL_H 10
#define DBL_CLICK_TICKS 35
#define WIN_ANIM_FRAMES 3
#define WIN_ANIM_DELAY_MS 8

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


static void app_open(app_kind_t app);
static void win_open(app_kind_t app, const char *title, int x, int y, int w, int h);

typedef struct {
    int used, x, y, w, h, z;
    char title[40];
    app_kind_t app;
    int animating;
    int anim_frame;
    int anim_dir;
    int anim_sx, anim_sy, anim_sw, anim_sh;
    int anim_ex, anim_ey, anim_ew, anim_eh;
} win_t;

typedef struct {
    char label[FS_MAX_NAME];
    app_kind_t app;        
    int x, y;
    u32 parent_dir_cluster; 
    u32 first_cluster;      
    int is_dir;             
} icon_t;

typedef struct {
    const char *label;
    app_kind_t app;
} MenuItem;

typedef struct {
    const char *title;
    int x, w;
    const MenuItem *items;
    int item_count;
} Menu;

static win_t wins[MAX_WIN];
static int z_seq;
static int drag_id, drag_off_x, drag_off_y;
static u8 prev_buttons;
int desktop_needs_full_blit = 1;

static int selected_icon = -1;
static int last_click_icon = -1;
static u32 last_click_ticks = 0;
static int last_click_x = 0;
static int last_click_y = 0;
static int last_click_disk_sel = -1;
static u32 last_click_disk_ticks = 0;
static int last_click_disk_x = 0;
static int last_click_disk_y = 0;
#define DBL_CLICK_DIST 4

static int menu_open = -1;
static int menu_hover = -1;
static int menu_hot = -1;

static int  disk_count[MAX_WIN];
static int  disk_sel[MAX_WIN];
static u32  disk_cwd_cluster[MAX_WIN];
static int  disk_parent_cluster[MAX_WIN];
static FSDirEnt disk_entries[MAX_WIN][FS_MAX_FILES];
static int  disk_clip_valid = 0;
static u32  disk_clip_src_dir = 0;
static char disk_clip_name[FS_MAX_NAME];

static void disk_refresh(int id) {
    if (id < 0 || id >= MAX_WIN) return;
    if (!wins[id].used || wins[id].app != APP_DISK) return;
    if (disk_cwd_cluster[id] < 2) disk_cwd_cluster[id] = fs_root_dir_cluster();
    disk_count[id] = fs_list_dir(disk_cwd_cluster[id], disk_entries[id], FS_MAX_FILES);
    if (disk_sel[id] >= disk_count[id]) disk_sel[id] = disk_count[id] - 1;
    if (disk_sel[id] < 0) disk_sel[id] = 0;
}

static void disk_make_new_folder_name(int id, char out[FS_MAX_NAME]) {
    
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
            if (str_eq(disk_entries[id][i].name, tmp)) { exists = 1; break; }
        }
        if (!exists) { str_cpy(out, tmp, FS_MAX_NAME); return; }
        n++;
        if (n > 9) break;
    }
    str_cpy(out, "NEWFOLD9", FS_MAX_NAME);
}

static void disk_copy_set(int id) {
    if (id < 0 || id >= MAX_WIN) return;
    int idx = disk_sel[id];
    if (idx < 0 || idx >= disk_count[id]) return;
    if (disk_entries[id][idx].is_dir) return; 
    disk_clip_valid = 1;
    disk_clip_src_dir = disk_cwd_cluster[id];
    str_cpy(disk_clip_name, disk_entries[id][idx].name, FS_MAX_NAME);
}

static int desktop_icons_dirty = 1;

static void desktop_mark_icons_dirty(void) {
    desktop_icons_dirty = 1;
}

static void disk_paste(int id) {
    if (id < 0 || id >= MAX_WIN) return;
    if (!disk_clip_valid) return;
    
    (void)fs_copy_file(disk_clip_src_dir, disk_clip_name, disk_cwd_cluster[id], disk_clip_name);
    disk_refresh(id);
    desktop_mark_icons_dirty();
}

static int str_cmp_simple(const char *a, const char *b) {
    if (!a) a = "";
    if (!b) b = "";
    for (int i = 0; a[i] || b[i]; i++) {
        unsigned char ca = (unsigned char)a[i];
        unsigned char cb = (unsigned char)b[i];
        if (ca != cb) return (int)ca - (int)cb;
        if (!a[i] && !b[i]) break;
    }
    return 0;
}

static void disk_sort_entries_by_name(int id) {
    if (id < 0 || id >= MAX_WIN || wins[id].app != APP_DISK) return;
    
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

static void open_file_in_notepad(u32 dir_cluster, const char* name);
static void open_disk_at_cluster(u32 dir_cluster, const char* title);


#define NOTEPAD_BUF_SIZE 4096
#define NOTEPAD_LINE_LEN 80
#define NOTEPAD_UNDO_SIZE 1024
#define NOTEPAD_CLIPBOARD_SIZE 1024
static char notepad_buf[MAX_WIN][NOTEPAD_BUF_SIZE];
static int  notepad_buf_len[MAX_WIN];
static int  notepad_cursor_pos[MAX_WIN];
static int  notepad_cursor_x[MAX_WIN];
static int  notepad_cursor_y[MAX_WIN];
static int  notepad_scroll_y[MAX_WIN];
static int  notepad_total_lines[MAX_WIN];
static int notepad_scrollbar_dragging = 0;  
static int notepad_scrollbar_drag_start_y = 0;
static int notepad_scrollbar_drag_start_scroll = 0;
static int notepad_manual_scroll_timer = 0;  


static char notepad_undo_buf[MAX_WIN][NOTEPAD_UNDO_SIZE];
static int notepad_undo_len[MAX_WIN];
static int notepad_undo_cursor[MAX_WIN];
static int notepad_has_undo[MAX_WIN];
static int notepad_has_redo[MAX_WIN];


static char notepad_clipboard[NOTEPAD_CLIPBOARD_SIZE];
static int notepad_clipboard_len = 0;


static int notepad_has_file[MAX_WIN];
static u32 notepad_file_dir_cluster[MAX_WIN];
static char notepad_file_name[MAX_WIN][FS_MAX_NAME];


static int notepad_find_has_pattern[MAX_WIN];
static char notepad_find_pattern[MAX_WIN][64];
static int notepad_find_from_pos[MAX_WIN]; 


static int notepad_sel_start[MAX_WIN];
static int notepad_sel_end[MAX_WIN];
static int notepad_has_selection[MAX_WIN];
static int notepad_sel_dragging = 0;  
static int notepad_sel_drag_start_pos[MAX_WIN];  


#define CALC_DISPLAY_LEN 32
static char calc_display[MAX_WIN][CALC_DISPLAY_LEN];
static double calc_accumulator[MAX_WIN];
static double calc_current[MAX_WIN];
static char calc_operation[MAX_WIN]; 
static int calc_has_decimal[MAX_WIN];
static int calc_new_entry[MAX_WIN];
static int calc_error[MAX_WIN];
static int calc_decimal_places[MAX_WIN];  
static int calc_left_decimal_places[MAX_WIN];  
static double calc_left_operand[MAX_WIN];  
static int calc_show_expression[MAX_WIN];  
static int calc_expr_complete[MAX_WIN];    


static int calc_scientific[MAX_WIN]; 
static char calc_clipboard[CALC_DISPLAY_LEN];
static int  calc_clipboard_len = 0;


static int ui_show_scrollbar_grid = 0;     
static int ui_clock_show_seconds = 0;      
static int calc_default_scientific = 0;   


static int puzzle_tiles[MAX_WIN][16];   
static int puzzle_empty_pos[MAX_WIN];  
static int puzzle_moves[MAX_WIN];
static int puzzle_solved[MAX_WIN];

 

static void calc_calculate(int id); 

static void calc_init(int id) {
    if (id < 0 || id >= MAX_WIN) return;
    calc_display[id][0] = '0';
    calc_display[id][1] = 0;
    calc_scientific[id] = calc_default_scientific;
    calc_accumulator[id] = 0;
    calc_current[id] = 0;
    calc_operation[id] = 0;
    calc_has_decimal[id] = 0;
    calc_decimal_places[id] = 0;
    calc_left_decimal_places[id] = 0;
    calc_new_entry[id] = 1;
    calc_error[id] = 0;
    calc_left_operand[id] = 0;
    calc_show_expression[id] = 0;
    calc_expr_complete[id] = 0;
}

static void calc_update_display(int id) {
    if (id < 0 || id >= MAX_WIN) return;
    if (calc_error[id]) {
        str_cpy(calc_display[id], "Error", CALC_DISPLAY_LEN);
        return;
    }
    
    
    auto void num_to_str(double val, char *buf, int max_len, int dec_places, int use_scientific) {
        if (use_scientific) {
            if (val == 0.0) {
                if (max_len > 1) { buf[0] = '0'; buf[1] = 0; }
                else if (max_len > 0) buf[0] = 0;
                return;
            }

            
            double absval = val < 0 ? -val : val;
            int exp = 0;

            while (absval >= 10.0 && exp < 99) { absval /= 10.0; exp++; }
            while (absval < 1.0 && exp > -99) { absval *= 10.0; exp--; }

            double mant = val < 0 ? -absval : absval;
            int prec = dec_places;
            if (prec <= 0) prec = 3;
            if (prec > 6) prec = 6;

            char mant_buf[32];
            num_to_str(mant, mant_buf, (int)sizeof(mant_buf), prec, 0);

            
            int pos = 0;
            for (int i = 0; mant_buf[i] && pos < max_len - 1; i++) buf[pos++] = mant_buf[i];
            if (pos < max_len - 1) buf[pos++] = 'E';

            if (pos < max_len - 1) buf[pos++] = (exp >= 0) ? '+' : '-';
            int eabs = exp >= 0 ? exp : -exp;

            if (eabs == 0) {
                if (pos < max_len - 1) buf[pos++] = '0';
            } else {
                char tmp[8];
                int tpos = 0;
                while (eabs > 0 && tpos < 7) {
                    tmp[tpos++] = (char)('0' + (eabs % 10));
                    eabs /= 10;
                }
                while (tpos > 0 && pos < max_len - 1) buf[pos++] = tmp[--tpos];
            }
            buf[pos] = 0;
            return;
        }

        
        if (dec_places == 0 && val == (int)val) {
            
            int v = (int)val;
            if (v < 0) v = -v;
            char temp[16];
            int pos = 0;
            if (v == 0) {
                temp[pos++] = '0';
            } else {
                while (v > 0 && pos < 15) {
                    temp[pos++] = '0' + (v % 10);
                    v /= 10;
                }
            }
            int i = 0;
            if (val < 0) buf[i++] = '-';
            for (int j = pos - 1; j >= 0 && i < max_len - 1; j--) {
                buf[i++] = temp[j];
            }
            buf[i] = 0;
        } else if (dec_places > 0) {
            
            long scaled, divisor = 1;
            int i = 0;
            
            
            for (int d = 0; d < dec_places; d++) divisor *= 10;
            
            
            double absval = val < 0 ? -val : val;
            
            absval += 0.000001;
            scaled = (long)((absval * divisor) + 0.5);
            
            long int_part = scaled / divisor;
            long dec_part = scaled % divisor;
            
            if (val < 0) buf[i++] = '-';
            
            
            if (int_part == 0) {
                buf[i++] = '0';
            } else {
                char temp[16];
                int pos = 0;
                long ip = int_part;
                while (ip > 0 && pos < 15) {
                    temp[pos++] = '0' + (ip % 10);
                    ip /= 10;
                }
                while (pos > 0 && i < max_len - 1) {
                    buf[i++] = temp[--pos];
                }
            }
            
            
            if (i < max_len - 1) buf[i++] = '.';
            
            
            long pad = divisor / 10;
            while (pad > 0 && dec_part < pad && i < max_len - 1) {
                buf[i++] = '0';
                pad /= 10;
            }
            
            
            if (dec_part == 0) {
                if (i < max_len - 1) buf[i++] = '0';
            } else {
                char temp[8];
                int pos = 0;
                long dp = dec_part;
                while (dp > 0 && pos < 7) {
                    temp[pos++] = '0' + (dp % 10);
                    dp /= 10;
                }
                while (pos > 0 && i < max_len - 1) {
                    buf[i++] = temp[--pos];
                }
            }
            buf[i] = 0;
        } else {
            
            int v = (int)val;
            if (v < 0) v = -v;
            char temp[16];
            int pos = 0;
            if (v == 0) {
                temp[pos++] = '0';
            } else {
                while (v > 0 && pos < 15) {
                    temp[pos++] = '0' + (v % 10);
                    v /= 10;
                }
            }
            int i = 0;
            if (val < 0) buf[i++] = '-';
            for (int j = pos - 1; j >= 0 && i < max_len - 1; j--) {
                buf[i++] = temp[j];
            }
            buf[i] = 0;
        }
    }
    
    char left_str[16] = {0};
    char right_str[16] = {0};
    char result_str[16] = {0};
    
    
    int sci = calc_scientific[id] ? 1 : 0;
    if (calc_expr_complete[id]) {
        
        num_to_str(calc_left_operand[id], left_str, 16, calc_left_decimal_places[id], sci);
        num_to_str(calc_current[id], right_str, 16, calc_decimal_places[id], sci);
        num_to_str(calc_current[id], result_str, 16, calc_decimal_places[id], sci);
        
        int i = 0;
        
        for (int j = 0; left_str[j] && i < CALC_DISPLAY_LEN - 1; j++)
            calc_display[id][i++] = left_str[j];
        
        if (i < CALC_DISPLAY_LEN - 1) calc_display[id][i++] = ' ';
        
        if (i < CALC_DISPLAY_LEN - 1) calc_display[id][i++] = calc_operation[id];
        
        if (i < CALC_DISPLAY_LEN - 1) calc_display[id][i++] = ' ';
        
        for (int j = 0; right_str[j] && i < CALC_DISPLAY_LEN - 1; j++)
            calc_display[id][i++] = right_str[j];
        
        if (i < CALC_DISPLAY_LEN - 3) {
            calc_display[id][i++] = ' ';
            calc_display[id][i++] = '=';
            calc_display[id][i++] = ' ';
        }
        
        for (int j = 0; result_str[j] && i < CALC_DISPLAY_LEN - 1; j++)
            calc_display[id][i++] = result_str[j];
        calc_display[id][i] = 0;
        
    } else if (calc_show_expression[id] && calc_operation[id]) {
        
        num_to_str(calc_left_operand[id], left_str, 16, calc_left_decimal_places[id], sci);
        num_to_str(calc_current[id], right_str, 16, calc_decimal_places[id], sci);
        
        int i = 0;
        
        for (int j = 0; left_str[j] && i < CALC_DISPLAY_LEN - 1; j++)
            calc_display[id][i++] = left_str[j];
        
        if (i < CALC_DISPLAY_LEN - 1) calc_display[id][i++] = ' ';
        
        if (i < CALC_DISPLAY_LEN - 1) calc_display[id][i++] = calc_operation[id];
        
        if (i < CALC_DISPLAY_LEN - 1) calc_display[id][i++] = ' ';
        
        for (int j = 0; right_str[j] && i < CALC_DISPLAY_LEN - 1; j++)
            calc_display[id][i++] = right_str[j];
        calc_display[id][i] = 0;
        
    } else {
        
        num_to_str(calc_current[id], calc_display[id], CALC_DISPLAY_LEN, calc_decimal_places[id], sci);
    }
}

static void calc_input_digit(int id, int digit) {
    if (id < 0 || id >= MAX_WIN) return;
    if (calc_error[id]) {
        calc_error[id] = 0;
        calc_current[id] = 0;
        calc_has_decimal[id] = 0;
        calc_decimal_places[id] = 0;
    }
    if (calc_new_entry[id]) {
        calc_current[id] = digit;
        calc_new_entry[id] = 0;
        calc_has_decimal[id] = 0;
        calc_decimal_places[id] = 0;
    } else {
        if (calc_has_decimal[id]) {
            
            calc_decimal_places[id]++;
            double decimal_factor = 1.0;
            for (int i = 0; i < calc_decimal_places[id]; i++) {
                decimal_factor *= 0.1;
            }
            calc_current[id] = calc_current[id] + digit * decimal_factor;
        } else {
            
            calc_current[id] = calc_current[id] * 10 + digit;
        }
    }
    calc_expr_complete[id] = 0;
    calc_update_display(id);
}

static void calc_input_decimal(int id) {
    if (id < 0 || id >= MAX_WIN) return;
    if (calc_error[id]) return;
    if (calc_new_entry[id]) {
        calc_current[id] = 0;
        calc_new_entry[id] = 0;
    }
    calc_has_decimal[id] = 1;
    calc_decimal_places[id] = 0;  
}

static void calc_clear(int id) {
    if (id < 0 || id >= MAX_WIN) return;
    calc_current[id] = 0;
    calc_accumulator[id] = 0;
    calc_operation[id] = 0;
    calc_has_decimal[id] = 0;
    calc_decimal_places[id] = 0;
    calc_left_decimal_places[id] = 0;
    calc_new_entry[id] = 1;
    calc_error[id] = 0;
    calc_left_operand[id] = 0;
    calc_show_expression[id] = 0;
    calc_expr_complete[id] = 0;
    calc_update_display(id);
}

static void calc_clear_entry(int id) {
    if (id < 0 || id >= MAX_WIN) return;
    
    calc_current[id] = 0;
    calc_has_decimal[id] = 0;
    calc_decimal_places[id] = 0;
    calc_new_entry[id] = 1;
    calc_error[id] = 0;
    calc_update_display(id);
}

static void calc_set_operation(int id, char op) {
    if (id < 0 || id >= MAX_WIN) return;
    if (calc_error[id]) return;
    
    if (calc_operation[id] && !calc_new_entry[id]) {
        calc_calculate(id);
    }
    calc_accumulator[id] = calc_current[id];
    calc_left_operand[id] = calc_current[id];  
    calc_left_decimal_places[id] = calc_decimal_places[id];  
    calc_operation[id] = op;
    calc_new_entry[id] = 1;
    calc_has_decimal[id] = 0;
    calc_decimal_places[id] = 0;
    calc_show_expression[id] = 1;  
    calc_expr_complete[id] = 0;
}

static void calc_calculate(int id) {
    if (id < 0 || id >= MAX_WIN) return;
    if (!calc_operation[id]) return;
    
    switch (calc_operation[id]) {
        case '+':
            calc_current[id] = calc_accumulator[id] + calc_current[id];
            break;
        case '-':
            calc_current[id] = calc_accumulator[id] - calc_current[id];
            break;
        case '*':
            calc_current[id] = calc_accumulator[id] * calc_current[id];
            break;
        case '/':
            if (calc_current[id] == 0) {
                calc_error[id] = 1;
            } else {
                calc_current[id] = calc_accumulator[id] / calc_current[id];
            }
            break;
    }
    calc_operation[id] = 0;
    calc_new_entry[id] = 1;
    calc_has_decimal[id] = 0;
    calc_decimal_places[id] = 0;
    calc_left_decimal_places[id] = 0;
    calc_accumulator[id] = calc_current[id];
    calc_show_expression[id] = 0;
    calc_expr_complete[id] = 0;
    calc_update_display(id);
}

static void calc_equals(int id) {
    if (id < 0 || id >= MAX_WIN) return;
    if (calc_error[id]) return;
    
    calc_expr_complete[id] = 1;
    calc_calculate(id);
}

static void handle_calc_key(int id, int key) {
    if (id < 0 || id >= MAX_WIN || wins[id].app != APP_CALC) return;
    
    if (key >= '0' && key <= '9') {
        calc_input_digit(id, key - '0');
        desktop_needs_full_blit = 1;
    } else if (key == '.' || key == ',') {
        calc_input_decimal(id);
        desktop_needs_full_blit = 1;
    } else if (key == '+') {
        calc_set_operation(id, '+');
        desktop_needs_full_blit = 1;
    } else if (key == '-') {
        calc_set_operation(id, '-');
        desktop_needs_full_blit = 1;
    } else if (key == '*') {
        calc_set_operation(id, '*');
        desktop_needs_full_blit = 1;
    } else if (key == '/') {
        calc_set_operation(id, '/');
        desktop_needs_full_blit = 1;
    } else if (key == '=' || key == KEY_ENTER) {
        calc_equals(id);
        desktop_needs_full_blit = 1;
    } else if (key == 'c' || key == 'C' || key == KEY_ESC) {
        calc_clear(id);
        desktop_needs_full_blit = 1;
    }
}


static const char *notepad_ctx_labels[] = { "Cut", "Copy", "Paste", "Clear", "Select All", "Redo" };
#define NOTEPAD_CTX_ITEMS 6


static int notepad_ctx_menu_open = 0;
static int notepad_ctx_menu_x = 0;
static int notepad_ctx_menu_y = 0;
static int notepad_ctx_menu_hover = -1;

static void draw_notepad_context_menu(void) {
    int i, w = 70, h;
    if (!notepad_ctx_menu_open) return;
    h = NOTEPAD_CTX_ITEMS * 11 + 6;
    
    if (notepad_ctx_menu_x + w > VGA13_WIDTH) notepad_ctx_menu_x = VGA13_WIDTH - w;
    if (notepad_ctx_menu_y + h > VGA13_HEIGHT) notepad_ctx_menu_y = VGA13_HEIGHT - h;
    
    vga13_fill_rect(notepad_ctx_menu_x + 1, notepad_ctx_menu_y + h, w, 1, PAL_DARK_GRAY);
    vga13_fill_rect(notepad_ctx_menu_x + w, notepad_ctx_menu_y + 1, 1, h, PAL_DARK_GRAY);
    vga13_fill_rect(notepad_ctx_menu_x, notepad_ctx_menu_y, w, h, VGA13_WHITE);
    
    hline_px(notepad_ctx_menu_x, notepad_ctx_menu_x + w - 1, notepad_ctx_menu_y, VGA13_BLACK);
    hline_px(notepad_ctx_menu_x, notepad_ctx_menu_x + w - 1, notepad_ctx_menu_y + h - 1, VGA13_BLACK);
    vline_px(notepad_ctx_menu_x, notepad_ctx_menu_y, notepad_ctx_menu_y + h - 1, VGA13_BLACK);
    vline_px(notepad_ctx_menu_x + w - 1, notepad_ctx_menu_y, notepad_ctx_menu_y + h - 1, VGA13_BLACK);
    
    for (i = 0; i < NOTEPAD_CTX_ITEMS; i++) {
        int iy = notepad_ctx_menu_y + 2 + i * 11;
        int inv = (i == notepad_ctx_menu_hover);
        if (inv) vga13_fill_rect(notepad_ctx_menu_x + 2, iy - 1, w - 4, 10, VGA13_BLACK);
        vga13_draw_string(notepad_ctx_menu_x + 8, iy, notepad_ctx_labels[i], VGA13_BLACK, VGA13_WHITE, inv);
    }
}


static int notepad_mouse_to_pos(int id, int mx, int my, int cx, int cy, int cw) {
    int char_width = 6;
    int chars_per_line = (cw - 4) / char_width;
    int line = (my - cy - 4) / 8 + notepad_scroll_y[id];
    int col = (mx - cx - 4) / char_width;
    
    if (col < 0) col = 0;
    if (col > chars_per_line) col = chars_per_line;
    if (line < 0) line = 0;
    
    
    int buf_pos = 0;
    int current_line = 0;
    int col_in_line = 0;
    
    while (buf_pos < notepad_buf_len[id]) {
        if (notepad_buf[id][buf_pos] == '\n') {
            if (current_line == line) {
                
                return buf_pos;
            }
            current_line++;
            col_in_line = 0;
        } else {
            if (current_line == line && col_in_line == col) {
                return buf_pos;
            }
            col_in_line++;
            if (col_in_line >= chars_per_line) {
                if (current_line == line) {
                    return buf_pos + 1;
                }
                current_line++;
                col_in_line = 0;
            }
        }
        buf_pos++;
    }
    
    return notepad_buf_len[id];
}

static void handle_notepad_click(int id, int mx, int my, int cx, int cy, int cw, int button) {
    int pos;
    
    if (id < 0 || id >= MAX_WIN) return;
    
    
    if (notepad_ctx_menu_open) {
        notepad_ctx_menu_open = 0;
        desktop_needs_full_blit = 1;
        if (button == 2) return;  
    }
    
    if (button == 0) {  
        pos = notepad_mouse_to_pos(id, mx, my, cx, cy, cw);
        
        if (notepad_sel_dragging < 0) {
            
            notepad_sel_dragging = id;
            notepad_sel_drag_start_pos[id] = pos;
            notepad_sel_start[id] = pos;
            notepad_sel_end[id] = pos;
            notepad_has_selection[id] = (pos < notepad_buf_len[id]);
            notepad_cursor_pos[id] = pos;
            desktop_needs_full_blit = 1;
        }
    } else if (button == 2) {  
        notepad_ctx_menu_x = mx;
        notepad_ctx_menu_y = my;
        notepad_ctx_menu_open = 1;
        notepad_ctx_menu_hover = -1;
        desktop_needs_full_blit = 1;
    }
}

static void handle_notepad_drag(int id, int mx, int my, int cx, int cy, int cw) {
    int pos;
    int start_pos;
    
    if (id < 0 || id >= MAX_WIN) return;
    if (notepad_sel_dragging != id) return;
    
    pos = notepad_mouse_to_pos(id, mx, my, cx, cy, cw);
    start_pos = notepad_sel_drag_start_pos[id];
    
    if (pos < start_pos) {
        notepad_sel_start[id] = pos;
        notepad_sel_end[id] = start_pos;
    } else {
        notepad_sel_start[id] = start_pos;
        notepad_sel_end[id] = pos;
    }
    
    notepad_has_selection[id] = (notepad_sel_start[id] < notepad_sel_end[id]);
    notepad_cursor_pos[id] = pos;
    desktop_needs_full_blit = 1;
}

static void notepad_end_drag(void) {
    notepad_sel_dragging = -1;
}


static void notepad_save_undo(int id) {
    int i;
    if (id < 0 || id >= MAX_WIN) return;
    
    for (i = 0; i < notepad_buf_len[id] && i < NOTEPAD_UNDO_SIZE - 1; i++) {
        notepad_undo_buf[id][i] = notepad_buf[id][i];
    }
    notepad_undo_len[id] = notepad_buf_len[id];
    notepad_undo_cursor[id] = notepad_cursor_pos[id];
    notepad_has_undo[id] = 1;
}

static void notepad_undo(int id) {
    int i;
    char temp_buf[NOTEPAD_BUF_SIZE];
    int temp_len, temp_cursor;
    if (id < 0 || id >= MAX_WIN || !notepad_has_undo[id]) return;
    
    for (i = 0; i < notepad_buf_len[id] && i < NOTEPAD_BUF_SIZE; i++) {
        temp_buf[i] = notepad_buf[id][i];
    }
    temp_len = notepad_buf_len[id];
    temp_cursor = notepad_cursor_pos[id];
    
    for (i = 0; i < notepad_undo_len[id] && i < NOTEPAD_BUF_SIZE; i++) {
        notepad_buf[id][i] = notepad_undo_buf[id][i];
    }
    notepad_buf_len[id] = notepad_undo_len[id];
    notepad_cursor_pos[id] = notepad_undo_cursor[id];
    
    for (i = 0; i < temp_len && i < NOTEPAD_UNDO_SIZE - 1; i++) {
        notepad_undo_buf[id][i] = temp_buf[i];
    }
    notepad_undo_len[id] = temp_len;
    notepad_undo_cursor[id] = temp_cursor;
    notepad_has_redo[id] = 1;
    notepad_has_undo[id] = 0;
    notepad_has_selection[id] = 0;
    desktop_needs_full_blit = 1;
}

static void notepad_redo(int id) {
    int i;
    char temp_buf[NOTEPAD_BUF_SIZE];
    int temp_len, temp_cursor;
    if (id < 0 || id >= MAX_WIN || !notepad_has_redo[id]) return;
    
    for (i = 0; i < notepad_buf_len[id] && i < NOTEPAD_BUF_SIZE; i++) {
        temp_buf[i] = notepad_buf[id][i];
    }
    temp_len = notepad_buf_len[id];
    temp_cursor = notepad_cursor_pos[id];
    
    for (i = 0; i < notepad_undo_len[id] && i < NOTEPAD_BUF_SIZE; i++) {
        notepad_buf[id][i] = notepad_undo_buf[id][i];
    }
    notepad_buf_len[id] = notepad_undo_len[id];
    notepad_cursor_pos[id] = notepad_undo_cursor[id];
    
    for (i = 0; i < temp_len && i < NOTEPAD_UNDO_SIZE - 1; i++) {
        notepad_undo_buf[id][i] = temp_buf[i];
    }
    notepad_undo_len[id] = temp_len;
    notepad_undo_cursor[id] = temp_cursor;
    notepad_has_undo[id] = 1;
    notepad_has_redo[id] = 0;
    notepad_has_selection[id] = 0;
    desktop_needs_full_blit = 1;
}

static void notepad_select_all(int id) {
    if (id < 0 || id >= MAX_WIN) return;
    notepad_sel_start[id] = 0;
    notepad_sel_end[id] = notepad_buf_len[id];
    notepad_has_selection[id] = 1;
    desktop_needs_full_blit = 1;
}

static void notepad_copy(int id) {
    int i, sel_start, sel_end;
    if (id < 0 || id >= MAX_WIN) return;
    
    if (notepad_has_selection[id]) {
        sel_start = notepad_sel_start[id];
        sel_end = notepad_sel_end[id];
    } else {
        sel_start = 0;
        sel_end = notepad_buf_len[id];
    }
    
    notepad_clipboard_len = 0;
    for (i = sel_start; i < sel_end && i < notepad_buf_len[id] && notepad_clipboard_len < NOTEPAD_CLIPBOARD_SIZE - 1; i++) {
        notepad_clipboard[notepad_clipboard_len++] = notepad_buf[id][i];
    }
    notepad_clipboard[notepad_clipboard_len] = 0;
}

static void notepad_paste(int id) {
    int i;
    if (id < 0 || id >= MAX_WIN || notepad_clipboard_len <= 0) return;
    
    notepad_save_undo(id);
    
    if (notepad_cursor_pos[id] > notepad_buf_len[id]) notepad_cursor_pos[id] = notepad_buf_len[id];
    
    if (notepad_buf_len[id] + notepad_clipboard_len >= NOTEPAD_BUF_SIZE) {
        
        int max_paste = NOTEPAD_BUF_SIZE - notepad_buf_len[id] - 1;
        if (max_paste < 0) max_paste = 0;
        notepad_clipboard_len = max_paste;
    }
    
    for (i = notepad_buf_len[id] + notepad_clipboard_len; i >= notepad_cursor_pos[id] + notepad_clipboard_len; i--) {
        notepad_buf[id][i] = notepad_buf[id][i - notepad_clipboard_len];
    }
    
    for (i = 0; i < notepad_clipboard_len; i++) {
        notepad_buf[id][notepad_cursor_pos[id] + i] = notepad_clipboard[i];
    }
    notepad_buf_len[id] += notepad_clipboard_len;
    notepad_buf[id][notepad_buf_len[id]] = 0;
    notepad_cursor_pos[id] += notepad_clipboard_len;
    notepad_has_selection[id] = 0;
    desktop_needs_full_blit = 1;
}

static void notepad_clear(int id) {
    if (id < 0 || id >= MAX_WIN) return;
    notepad_save_undo(id);
    notepad_buf_len[id] = 0;
    notepad_buf[id][0] = 0;
    notepad_cursor_pos[id] = 0;
    notepad_has_selection[id] = 0;
    desktop_needs_full_blit = 1;
}

static void notepad_cut(int id) {
    notepad_copy(id);
    notepad_clear(id);
}



static void notepad_set_file_association(int id, u32 dir_cluster, const char *name) {
    if (id < 0 || id >= MAX_WIN) return;
    if (!name || !name[0]) {
        notepad_has_file[id] = 0;
        return;
    }
    notepad_has_file[id] = 1;
    notepad_file_dir_cluster[id] = dir_cluster;
    str_cpy(notepad_file_name[id], name, FS_MAX_NAME);
}

static void notepad_write_buffer_to_file(int id, const char *name83) {
    if (id < 0 || id >= MAX_WIN) return;
    if (!name83 || !name83[0]) return;

    
    int fd = fs_open(name83);
    if (fd < 0) {
        fd = fs_create(name83);
    }
    if (fd < 0) return;

    (void)fs_write(fd, notepad_buf[id], (u32)notepad_buf_len[id]);
}

static void notepad_save(int id) {
    if (id < 0 || id >= MAX_WIN || wins[id].app != APP_NOTEPAD) return;
    if (!notepad_has_file[id]) {
        
        const char *title = wins[id].title;
        if (title && title[0] && !str_eq(title, "Notepad")) notepad_set_file_association(id, fs_root_dir_cluster(), title);
        else notepad_set_file_association(id, fs_root_dir_cluster(), "NOTES.TXT");
    }
    notepad_write_buffer_to_file(id, notepad_file_name[id]);
}

static void notepad_save_as(int id) {
    if (id < 0 || id >= MAX_WIN || wins[id].app != APP_NOTEPAD) return;
    const char *title = wins[id].title;
    if (!title || !title[0] || str_eq(title, "Notepad")) {
        title = "NOTES.TXT";
    }
    notepad_set_file_association(id, notepad_has_file[id] ? notepad_file_dir_cluster[id] : fs_root_dir_cluster(), title);
    notepad_write_buffer_to_file(id, notepad_file_name[id]);
}

static int notepad_is_word_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || (c == '_');
}

static void notepad_extract_token(int id, char *out, int out_cap) {
    if (!out || out_cap <= 1 || id < 0 || id >= MAX_WIN) return;
    out[0] = 0;
    if (notepad_buf_len[id] <= 0) return;

    int pos = notepad_cursor_pos[id];
    if (pos < 0) pos = 0;
    if (pos > notepad_buf_len[id]) pos = notepad_buf_len[id];

    int idx = (pos > 0) ? (pos - 1) : 0;
    if (idx < 0 || idx >= notepad_buf_len[id]) return;
    if (!notepad_is_word_char(notepad_buf[id][idx])) return;

    int start = idx;
    while (start > 0 && notepad_is_word_char(notepad_buf[id][start - 1])) start--;
    int end = idx + 1;
    while (end < notepad_buf_len[id] && notepad_is_word_char(notepad_buf[id][end])) end++;

    int len = end - start;
    if (len >= out_cap) len = out_cap - 1;
    for (int i = 0; i < len; i++) out[i] = notepad_buf[id][start + i];
    out[len] = 0;
}

static int notepad_find_pattern_idx(int id, const char *pattern, int start_pos, int wrap) {
    int pat_len = str_len(pattern);
    if (pat_len <= 0) return -1;
    int max_start = notepad_buf_len[id] - pat_len;
    if (max_start < 0) return -1;

    for (int i = start_pos; i <= max_start; i++) {
        int j;
        for (j = 0; j < pat_len; j++) {
            if (notepad_buf[id][i + j] != pattern[j]) break;
        }
        if (j == pat_len) return i;
    }

    if (wrap) {
        for (int i = 0; i < start_pos; i++) {
            if (i > max_start) break;
            int j;
            for (j = 0; j < pat_len; j++) {
                if (notepad_buf[id][i + j] != pattern[j]) break;
            }
            if (j == pat_len) return i;
        }
    }
    return -1;
}

static void notepad_find_next(int id, int reset_pattern) {
    if (id < 0 || id >= MAX_WIN || wins[id].app != APP_NOTEPAD) return;

    char token[64];
    if (reset_pattern || !notepad_find_has_pattern[id]) {
        notepad_extract_token(id, token, sizeof(token));
        if (!token[0]) return;
        notepad_find_has_pattern[id] = 1;
        str_cpy(notepad_find_pattern[id], token, 64);
        notepad_find_from_pos[id] = notepad_cursor_pos[id];
    }

    int start = notepad_find_from_pos[id];
    if (start < 0) start = 0;
    if (start > notepad_buf_len[id]) start = notepad_buf_len[id];

    int match = notepad_find_pattern_idx(id, notepad_find_pattern[id], start, 1);
    if (match < 0) return;

    int pat_len = str_len(notepad_find_pattern[id]);
    notepad_sel_start[id] = match;
    notepad_sel_end[id] = match + pat_len;
    notepad_has_selection[id] = 1;
    notepad_cursor_pos[id] = notepad_sel_end[id];
    notepad_find_from_pos[id] = notepad_sel_end[id];
    desktop_needs_full_blit = 1;
}


static int hit_notepad_context_menu(int mx, int my) {
    int w = 70, h;
    if (!notepad_ctx_menu_open) return -1;
    h = NOTEPAD_CTX_ITEMS * 11 + 6;
    if (mx < notepad_ctx_menu_x || mx >= notepad_ctx_menu_x + w ||
        my < notepad_ctx_menu_y || my >= notepad_ctx_menu_y + h) return -1;
    return (my - notepad_ctx_menu_y - 3) / 11;
}


static void notepad_context_menu_click(int mx, int my, int id) {
    int item = hit_notepad_context_menu(mx, my);
    if (item >= 0 && item < NOTEPAD_CTX_ITEMS && id >= 0 && id < MAX_WIN) {
        if (item == 0) { 
            notepad_cut(id);
        } else if (item == 1) { 
            notepad_copy(id);
        } else if (item == 2) { 
            notepad_paste(id);
        } else if (item == 3) { 
            notepad_clear(id);
        } else if (item == 4) { 
            notepad_select_all(id);
        } else if (item == 5) { 
            notepad_redo(id);
        }
    }
    notepad_ctx_menu_open = 0;
    notepad_ctx_menu_hover = -1;
    desktop_needs_full_blit = 1;
}


#define MAX_DESKTOP_ICONS 64
static icon_t desktop_icons_data[MAX_DESKTOP_ICONS];
static icon_t *desktop_icons = desktop_icons_data;
static int desktop_icon_count = 0;
static u32 desktop_folder_cluster = 0;


static int selected_icons[MAX_DESKTOP_ICONS];
static int num_selected_icons = 0;
static int shift_held = 0;


static int desktop_clip_valid = 0;
static int desktop_clip_move = 0; 
static u32 desktop_clip_src_dir = 0;
static int desktop_clip_is_dir = 0;
static char desktop_clip_name[FS_MAX_NAME];


static int icon_dragging = 0;
static int icon_drag_idx = -1;
static int icon_drag_off_x = 0;
static int icon_drag_off_y = 0;


static int lasso_active = 0;
static int lasso_start_x = 0;
static int lasso_start_y = 0;
static int lasso_cur_x = 0;
static int lasso_cur_y = 0;


static int ctx_menu_open = 0;
static int ctx_menu_x = 0;
static int ctx_menu_y = 0;
static int ctx_menu_hover = -1;
static int ctx_menu_target_icon = -1; 

#define CTX_MENU_ITEMS 6
static const char *ctx_menu_labels[CTX_MENU_ITEMS] = {
    "New Folder",
    "Open",
    "Get Info",
    "Rename",
    "Move to Trash",
    "Eject"
};


static int calc_ctx_menu_open = 0;
static int calc_ctx_menu_x = 0;
static int calc_ctx_menu_y = 0;
static int calc_ctx_menu_hover = -1;
#define CALC_CTX_MENU_ITEMS 4
static const char *calc_ctx_menu_labels[CALC_CTX_MENU_ITEMS] = { "Copy", "Paste", "Standard", "Scientific" };


static int disk_ctx_menu_open = 0;
static int disk_ctx_menu_x = 0;
static int disk_ctx_menu_y = 0;
static int disk_ctx_menu_hover = -1;
static int disk_ctx_target_idx = -1; 
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

static void desktop_add_icon_app(const char* label, app_kind_t app, int x, int y) {
    if (desktop_icon_count >= MAX_DESKTOP_ICONS) return;
    icon_t* ic = &desktop_icons[desktop_icon_count++];
    kmemset(ic, 0, sizeof(*ic));
    str_cpy(ic->label, label, FS_MAX_NAME);
    ic->app = app;
    ic->x = x;
    ic->y = y;
}

static void desktop_add_icon_fs(const char* label, u32 parent_dir, u32 first_cluster, int is_dir, int x, int y) {
    if (desktop_icon_count >= MAX_DESKTOP_ICONS) return;
    icon_t* ic = &desktop_icons[desktop_icon_count++];
    kmemset(ic, 0, sizeof(*ic));
    str_cpy(ic->label, label, FS_MAX_NAME);
    ic->app = APP_NONE;
    ic->parent_dir_cluster = parent_dir;
    ic->first_cluster = first_cluster;
    ic->is_dir = is_dir;
    ic->x = x;
    ic->y = y;
}

static void desktop_build_icons(void) {
    
    icon_t old[MAX_DESKTOP_ICONS];
    int old_n = desktop_icon_count;
    for (int i = 0; i < old_n; i++) old[i] = desktop_icons[i];

    desktop_icon_count = 0;
    desktop_folder_cluster = 0;

    
    desktop_add_icon_app("Computer", APP_ABOUT,    16, 22);
    desktop_add_icon_app("Notepad",  APP_NOTEPAD,  16, 62);
    desktop_add_icon_app("Calc",     APP_CALC,     16, 102);
    desktop_add_icon_app("Term",     APP_TERMINAL, 16, 142);
    desktop_add_icon_app("HD",       APP_DISK,     VGA13_WIDTH - 31, 22);
    desktop_add_icon_app("Trash",    APP_TRASH,    VGA13_WIDTH - 31, VGA13_HEIGHT - 42);

    
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
    if (fat32_find_in_dir(fat32_get_root_cluster(), "DESKTOP", &e) != 0) return;
    desktop_folder_cluster = ((u32)e.cluster_high << 16) | (u32)e.cluster_low;
    if (desktop_folder_cluster < 2) return;

    
    FSDirEnt ents[FS_MAX_FILES];
    int n = fs_list_dir(desktop_folder_cluster, ents, FS_MAX_FILES);

    int gx = 50;
    int gy = 22;
    int col_w = 36;
    int row_h = 40;
    int cols = (VGA13_WIDTH - 80) / col_w;
    if (cols < 1) cols = 1;

    int k = 0;
    for (int i = 0; i < n && desktop_icon_count < MAX_DESKTOP_ICONS; i++) {
        
        if (!ents[i].name[0]) continue;
        int x = gx + (k % cols) * col_w;
        int y = gy + (k / cols) * row_h;
        if (y > VGA13_HEIGHT - 60) break;
        desktop_add_icon_fs(ents[i].name, desktop_folder_cluster, ents[i].first_cluster, ents[i].is_dir, x, y);

        
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

static const MenuItem menu_sav_items[] = {
    { "About SavaOS", APP_ABOUT },
    { "Control Panel", APP_CONTROL_PANEL },
    { "Puzzle", APP_PUZZLE },
    { "Shut Down", APP_NONE }
};


static const MenuItem menu_desk_file_items[] = {
    { "New Folder", APP_NONE },
    { "Open", APP_NONE },
    { "Close", APP_NONE },
    { "Get Info", APP_NONE },
    { "Duplicate", APP_NONE }
};
static const MenuItem menu_desk_edit_items[] = {
    { "Undo", APP_NONE },
    { "Cut", APP_NONE },
    { "Copy", APP_NONE },
    { "Paste", APP_NONE },
    { "Select All", APP_NONE }
};
static const MenuItem menu_desk_view_items[] = {
    { "By Icon", APP_NONE },
    { "By Name", APP_NONE },
    { "By Date", APP_NONE },
    { "Clean Up", APP_NONE }
};
static const MenuItem menu_desk_special_items[] = {
    { "Eject Disk", APP_NONE },
    { "Erase Disk", APP_NONE },
    { "Set Startup", APP_NONE }
};


static const MenuItem menu_np_file_items[] = {
    { "New", APP_NOTEPAD },
    { "Open...", APP_NONE },
    { "Save", APP_NONE },
    { "Save As...", APP_NONE },
    { "Close", APP_NONE }
};
static const MenuItem menu_np_edit_items[] = {
    { "Undo", APP_NONE },
    { "Redo", APP_NONE },
    { "Cut", APP_NONE },
    { "Copy", APP_NONE },
    { "Paste", APP_NONE },
    { "Clear", APP_NONE },
    { "Select All", APP_NONE }
};
static const MenuItem menu_np_search_items[] = {
    { "Find...", APP_NONE },
    { "Find Again", APP_NONE }
};
static const MenuItem menu_np_format_items[] = {
    { "Font...", APP_NONE },
    { "Style", APP_NONE }
};


static const MenuItem menu_calc_edit_items[] = {
    { "Copy", APP_NONE },
    { "Paste", APP_NONE }
};
static const MenuItem menu_calc_view_items[] = {
    { "Standard", APP_NONE },
    { "Scientific", APP_NONE }
};


static const MenuItem menu_term_shell_items[] = {
    { "New Window", APP_TERMINAL },
    { "Close", APP_NONE },
    { "Run...", APP_NONE }
};
static const MenuItem menu_term_edit_items[] = {
    { "Cut", APP_NONE },
    { "Copy", APP_NONE },
    { "Paste", APP_NONE },
    { "Clear", APP_NONE }
};


static const MenuItem menu_disk_file_items[] = {
    { "New Folder", APP_NONE },
    { "Open", APP_NONE },
    { "Close Window", APP_NONE }
};
static const MenuItem menu_disk_edit_items[] = {
    { "Cut", APP_NONE },
    { "Copy", APP_NONE },
    { "Paste", APP_NONE },
    { "Delete", APP_NONE }
};


static Menu desk_menus[] = {
    { "@", 0, 0, menu_sav_items, 4 },
    { "File", 0, 0, menu_desk_file_items, 5 },
    { "Edit", 0, 0, menu_desk_edit_items, 5 },
    { "View", 0, 0, menu_desk_view_items, 4 },
    { "Special", 0, 0, menu_desk_special_items, 3 }
};
static Menu np_menus[] = {
    { "@", 0, 0, menu_sav_items, 4 },
    { "File", 0, 0, menu_np_file_items, 5 },
    { "Edit", 0, 0, menu_np_edit_items, 7 },
    { "Search", 0, 0, menu_np_search_items, 2 },
    { "Format", 0, 0, menu_np_format_items, 2 }
};
static Menu calc_menus[] = {
    { "@", 0, 0, menu_sav_items, 4 },
    { "Edit", 0, 0, menu_calc_edit_items, 2 },
    { "View", 0, 0, menu_calc_view_items, 2 }
};
static Menu term_menus[] = {
    { "@", 0, 0, menu_sav_items, 4 },
    { "Shell", 0, 0, menu_term_shell_items, 3 },
    { "Edit", 0, 0, menu_term_edit_items, 4 },
    { "View", 0, 0, menu_calc_view_items, 2 }
};
static Menu disk_menus[] = {
    { "@", 0, 0, menu_sav_items, 4 },
    { "File", 0, 0, menu_disk_file_items, 3 },
    { "Edit", 0, 0, menu_disk_edit_items, 4 },
    { "View", 0, 0, menu_desk_view_items, 4 },
    { "Special", 0, 0, menu_desk_special_items, 3 }
};


static Menu sav_only_menus[] = {
    { "@", 0, 0, menu_sav_items, 4 }
};


static Menu *current_menus = desk_menus;
static int current_menu_count = 5;
static app_kind_t current_menu_app = APP_NONE;


static Menu *menus = desk_menus;
#define MENU_COUNT current_menu_count

void str_cpy(char *d, const char *s, int max) { int i; for (i = 0; i < max - 1 && s[i]; i++) d[i] = s[i]; d[i] = 0; }
int str_len(const char *s) { int i = 0; while (s && s[i]) i++; return i; }
int str_eq(const char *a, const char *b) {
    int i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return a[i] == 0 && b[i] == 0;
}
int str_starts_with(const char *s, const char *pfx) {
    int i = 0;
    while (pfx[i]) {
        if (s[i] != pfx[i]) return 0;
        i++;
    }
    return 1;
}
void str_copy_n(char *dst, const char *src, int max) {
    int i = 0;
    if (max <= 0) return;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static void open_file_in_notepad(u32 dir_cluster, const char* name) {
    if (!name || !name[0]) return;
    int np = -1;
    for (int i = 0; i < MAX_WIN; i++) {
        if (wins[i].used && wins[i].app == APP_NOTEPAD) { np = i; break; }
    }
    if (np < 0) {
        app_open(APP_NOTEPAD);
        np = win_top_id();
    }
    if (np < 0) return;
    bring_to_front(np);

    char tmp[NOTEPAD_BUF_SIZE];
    int n = fs_read_file_in_dir(dir_cluster, name, tmp, NOTEPAD_BUF_SIZE - 1);
    if (n < 0) n = 0;
    tmp[n] = 0;

    
    notepad_buf_len[np] = 0;
    for (int i = 0; tmp[i] && notepad_buf_len[np] < NOTEPAD_BUF_SIZE - 1; i++) {
        if (tmp[i] == '\r') continue;
        notepad_buf[np][notepad_buf_len[np]++] = tmp[i];
    }
    notepad_buf[np][notepad_buf_len[np]] = 0;
    notepad_cursor_pos[np] = notepad_buf_len[np];
    notepad_scroll_y[np] = 0;
    notepad_has_selection[np] = 0;

    
    notepad_set_file_association(np, dir_cluster, name);
    notepad_undo_len[np] = 0;
    notepad_has_undo[np] = 0;
    notepad_has_redo[np] = 0;
    notepad_clipboard_len = 0;
    notepad_find_has_pattern[np] = 0;
    notepad_find_pattern[np][0] = 0;
    notepad_find_from_pos[np] = 0;
    notepad_sel_dragging = -1;
    notepad_scrollbar_dragging = -1;

    str_cpy(wins[np].title, name, sizeof(wins[np].title));
    desktop_needs_full_blit = 1;
}

static void open_disk_at_cluster(u32 dir_cluster, const char* title) {
    if (dir_cluster < 2) return;
    win_open(APP_DISK, title ? title : "Disk", 60, 40, 200, 140);
    int id = win_top_id();
    if (id >= 0 && wins[id].app == APP_DISK) {
        disk_cwd_cluster[id] = dir_cluster;
        disk_parent_cluster[id] = 0;
        disk_sel[id] = 0;
        disk_refresh(id);
    }
    desktop_needs_full_blit = 1;
}
static void hline_px(int x1, int x2, int y, u8 c) {
    int x, t;
    if (y < 0 || y >= VGA13_HEIGHT) return;
    if (x1 > x2) { t = x1; x1 = x2; x2 = t; }
    if (x1 < 0) x1 = 0;
    if (x2 >= VGA13_WIDTH) x2 = VGA13_WIDTH - 1;
    if (x1 > x2) return;
    for (x = x1; x <= x2; x++) vga13_put_pixel(x, y, c);
}
static void vline_px(int x, int y1, int y2, u8 c) {
    int y, t;
    if (x < 0 || x >= VGA13_WIDTH) return;
    if (y1 > y2) { t = y1; y1 = y2; y2 = t; }
    if (y1 < 0) y1 = 0;
    if (y2 >= VGA13_HEIGHT) y2 = VGA13_HEIGHT - 1;
    if (y1 > y2) return;
    for (y = y1; y <= y2; y++) vga13_put_pixel(x, y, c);
}

static void draw_window_outline(int x, int y, int w, int h) {
    hline_px(x, x + w - 1, y, VGA13_BLACK);
    hline_px(x, x + w - 1, y + h - 1, VGA13_BLACK);
    vline_px(x, y, y + h - 1, VGA13_BLACK);
    vline_px(x + w - 1, y, y + h - 1, VGA13_BLACK);
}

static void animate_window_zoom(int id) {
    int x, y, w, h;
    float t;
    if (id < 0 || id >= MAX_WIN || !wins[id].used || !wins[id].animating) return;
    t = (float)wins[id].anim_frame / (float)WIN_ANIM_FRAMES;
    if (wins[id].anim_dir == 1) {
        x = (int)(wins[id].anim_sx + (wins[id].anim_ex - wins[id].anim_sx) * t);
        y = (int)(wins[id].anim_sy + (wins[id].anim_ey - wins[id].anim_sy) * t);
        w = (int)(wins[id].anim_sw + (wins[id].anim_ew - wins[id].anim_sw) * t);
        h = (int)(wins[id].anim_sh + (wins[id].anim_eh - wins[id].anim_sh) * t);
    } else {
        t = 1.0f - t;
        x = (int)(wins[id].anim_ex + (wins[id].anim_sx - wins[id].anim_ex) * t);
        y = (int)(wins[id].anim_ey + (wins[id].anim_sy - wins[id].anim_ey) * t);
        w = (int)(wins[id].anim_ew + (wins[id].anim_sw - wins[id].anim_ew) * t);
        h = (int)(wins[id].anim_eh + (wins[id].anim_sh - wins[id].anim_eh) * t);
    }
    if (w < 4) w = 4;
    if (h < 4) h = 4;
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

static void win_start_open_anim(int id, int ix, int iy, int x, int y, int w, int h) {
    if (id < 0 || id >= MAX_WIN) return;
    wins[id].animating = 1;
    wins[id].anim_frame = 0;
    wins[id].anim_dir = 1;
    wins[id].anim_sx = ix + ICON_SIZE/2;
    wins[id].anim_sy = iy + ICON_SIZE/2;
    wins[id].anim_sw = 8;
    wins[id].anim_sh = 8;
    wins[id].anim_ex = x;
    wins[id].anim_ey = y;
    wins[id].anim_ew = w;
    wins[id].anim_eh = h;
}

static void win_start_close_anim(int id) {
    int i;
    if (id < 0 || id >= MAX_WIN || !wins[id].used) return;
    desktop_ensure_icons_built();
    for (i = 0; i < desktop_icon_count; i++) {
        if (desktop_icons[i].app == wins[id].app) {
            wins[id].anim_ex = desktop_icons[i].x + ICON_SIZE/2;
            wins[id].anim_ey = desktop_icons[i].y + ICON_SIZE/2;
            wins[id].anim_ew = 8;
            wins[id].anim_eh = 8;
            break;
        }
    }
    wins[id].anim_sx = wins[id].x;
    wins[id].anim_sy = wins[id].y;
    wins[id].anim_sw = wins[id].w;
    wins[id].anim_sh = wins[id].h;
    wins[id].animating = 1;
    wins[id].anim_frame = 0;
    wins[id].anim_dir = -1;
}

static void switch_menus_for_app(app_kind_t app);

static int win_max_z(void) { int m = 0, i; for (i = 0; i < MAX_WIN; i++) if (wins[i].used && wins[i].z > m) m = wins[i].z; return m; }
static int win_top_id(void) { int i, best = -1, best_z = -1; for (i = 0; i < MAX_WIN; i++) if (wins[i].used && wins[i].z > best_z) { best_z = wins[i].z; best = i; } return best; }
static void win_bring_front(int id) { 
    if (id < 0 || id >= MAX_WIN || !wins[id].used) return; 
    wins[id].z = win_max_z() + 1; 
    switch_menus_for_app(wins[id].app);
}
static void bring_to_front(int win_idx) { win_bring_front(win_idx); }
static int win_alloc(void) { int i; for (i = 0; i < MAX_WIN; i++) if (!wins[i].used) return i; return -1; }
static void win_close(int id) {
    if (id >= 0 && id < MAX_WIN && wins[id].used && !wins[id].animating) {
        win_start_close_anim(id);
        desktop_needs_full_blit = 1;
    }
}

static void win_open(app_kind_t app, const char *title, int x, int y, int w, int h) {
    int id = win_alloc();
    int i, ix = x, iy = y;
    if (id < 0) return;
    desktop_ensure_icons_built();
    for (i = 0; i < desktop_icon_count; i++) {
        if (desktop_icons[i].app == app) {
            ix = desktop_icons[i].x;
            iy = desktop_icons[i].y;
            break;
        }
    }
    wins[id].used = 1;
    wins[id].x = x; wins[id].y = y; wins[id].w = w; wins[id].h = h;
    wins[id].app = app;
    wins[id].animating = 0;
    str_cpy(wins[id].title, title, sizeof(wins[id].title));
    wins[id].z = ++z_seq;
    if (app == APP_TERMINAL) {
        term_window_init(id);
    }
    if (app == APP_DISK) {
        
        fs_try_mount_fat32();
        disk_cwd_cluster[id] = fs_root_dir_cluster();
        disk_parent_cluster[id] = 0;
        disk_count[id] = fs_list_dir(disk_cwd_cluster[id], disk_entries[id], FS_MAX_FILES);
        disk_sel[id] = 0;
    }
    if (app == APP_NOTEPAD) {
        notepad_buf_len[id] = 0;
        notepad_buf[id][0] = 0;
        notepad_cursor_pos[id] = 0;
        notepad_cursor_x[id] = 0;
        notepad_cursor_y[id] = 0;
        notepad_scroll_y[id] = 0;
        notepad_total_lines[id] = 1;

        notepad_has_file[id] = 0;
        notepad_file_dir_cluster[id] = 0;
        notepad_file_name[id][0] = 0;
        notepad_find_has_pattern[id] = 0;
        notepad_find_pattern[id][0] = 0;
        notepad_find_from_pos[id] = 0;
        notepad_clipboard_len = 0;
        
        notepad_undo_len[id] = 0;
        notepad_undo_cursor[id] = 0;
        notepad_has_undo[id] = 0;
        notepad_has_redo[id] = 0;
        
        notepad_sel_start[id] = 0;
        notepad_sel_end[id] = 0;
        notepad_has_selection[id] = 0;
        
        {
            const char *welcome = "Welcome to Notepad!\nType your notes here...\n\nFeatures:\n- Type text\n- Arrow keys\n- Enter for new line\n- Backspace to delete";
            int i;
            for (i = 0; welcome[i] && i < NOTEPAD_BUF_SIZE - 1; i++) {
                notepad_buf[id][i] = welcome[i];
            }
            notepad_buf[id][i] = 0;
            notepad_buf_len[id] = i;
            notepad_cursor_pos[id] = i;
            
            notepad_total_lines[id] = 1;
            for (i = 0; i < notepad_buf_len[id]; i++) {
                if (notepad_buf[id][i] == '\n') notepad_total_lines[id]++;
            }
        }
    }
    if (app == APP_CALC) {
        calc_init(id);
    }
    if (app == APP_PUZZLE) {
        puzzle_init(id);
    }
    win_start_open_anim(id, ix, iy, x, y, w, h);
    desktop_needs_full_blit = 1;
}

static void app_open(app_kind_t app) {
    switch (app) {
    case APP_ABOUT: win_open(APP_ABOUT, "About", 50, 30, 200, 120); break;
    case APP_CONTROL_PANEL: win_open(APP_CONTROL_PANEL, "Control Panel", 55, 30, 213, 150); break;
    case APP_PUZZLE: win_open(APP_PUZZLE, "Puzzle", 50, 35, 130, 155); break;
    case APP_NOTEPAD: win_open(APP_NOTEPAD, "Notepad", 70, 40, 180, 130); break;
    case APP_CALC: win_open(APP_CALC, "Calc", 85, 40, 95, 125); break;
    case APP_TERMINAL: win_open(APP_TERMINAL, "Terminal", 70, 55, 200, 100); break;
    case APP_DISK: win_open(APP_DISK, "System HD", 56, 30, 210, 138); break;
    case APP_TRASH: win_open(APP_TRASH, "Trash", 90, 45, 170, 110); break;
    default: break;
    }
    desktop_needs_full_blit = 1;
}

static void win_clamp(int id) {
    if (id < 0 || !wins[id].used) return;
    if (wins[id].x < 0) wins[id].x = 0;
    if (wins[id].y < DESKTOP_Y) wins[id].y = DESKTOP_Y;
    if (wins[id].x + wins[id].w > VGA13_WIDTH) wins[id].x = VGA13_WIDTH - wins[id].w;
    if (wins[id].y + wins[id].h > VGA13_HEIGHT) wins[id].y = VGA13_HEIGHT - wins[id].h;
}

static int hit_top_window(int mx, int my) {
    int best = -1, best_z = -1, i;
    for (i = 0; i < MAX_WIN; i++) {
        if (!wins[i].used || wins[i].animating) continue;
        if (mx >= wins[i].x && mx < wins[i].x + wins[i].w &&
            my >= wins[i].y && my < wins[i].y + wins[i].h &&
            wins[i].z > best_z) {
            best_z = wins[i].z;
            best = i;
        }
    }
    return best;
}

static int hit_close(int id, int mx, int my) {
    int bx, by;
    if (id < 0 || !wins[id].used) return 0;
    bx = wins[id].x + 4;  
    by = wins[id].y + 2;
    return mx >= bx && mx < bx + SVS_CTRL_SIZE && my >= by && my < by + SVS_CTRL_SIZE;
}

static int hit_title_drag(int id, int mx, int my) {
    int tx0, tx1;
    if (id < 0 || !wins[id].used) return 0;
    if (my < wins[id].y + 1 || my >= wins[id].y + SVS_TITLE_H) return 0;
    tx0 = wins[id].x + 2;
    tx1 = wins[id].x + wins[id].w - SVS_CTRL_SIZE - 7;
    return mx >= tx0 && mx < tx1;
}

static void draw_client_about(int cx, int cy, int cw, int ch) {
    int mid = cx + cw / 2;
    int rx  = cx + cw - 1;

    vga13_fill_rect(cx, cy, cw, ch, VGA13_WHITE);

    int title_x = mid - 18;
    vga13_draw_string(title_x,     cy + 8,  "SavaOS", VGA13_BLACK, VGA13_WHITE, 0);
 
    draw_text_centered(cx, cy + 24, cw, 11, "Version 0.1 32-bit",
                       VGA13_BLACK, VGA13_WHITE);

    hline_px(cx, rx, cy + 39, VGA13_BLACK);

    int lx = cx + 20, vx = cx + 70;
    vga13_draw_string(lx, cy + 49, "RAM:",        VGA13_BLACK, VGA13_WHITE, 0);
    vga13_draw_string(vx, cy + 49, "32768 KB",     VGA13_BLACK, VGA13_WHITE, 0);
    vga13_draw_string(lx, cy + 63, "HD:",  VGA13_BLACK, VGA13_WHITE, 0);
    vga13_draw_string(vx, cy + 63, "System HD",     VGA13_BLACK, VGA13_WHITE, 0);
    vga13_draw_string(lx, cy + 77, "Mode:",       VGA13_BLACK, VGA13_WHITE, 0);
    vga13_draw_string(vx, cy + 77, "Development", VGA13_BLACK, VGA13_WHITE, 0);
}





static void draw_radio(int cx, int cy, int filled) {
    int i;
    static const int pts[][2] = {
        {2,0},{3,0},{4,0},{5,0},{6,0},
        {1,1},{7,1},
        {0,2},{8,2},{0,3},{8,3},{0,4},{8,4},
        {0,5},{8,5},{0,6},{8,6},
        {1,7},{7,7},
        {2,8},{3,8},{4,8},{5,8},{6,8},
    };
    int n = (int)(sizeof(pts)/sizeof(pts[0]));
    for (i = 0; i < n; i++)
        vga13_put_pixel(cx + pts[i][0], cy + pts[i][1], VGA13_BLACK);
    if (filled) {
        vga13_fill_rect(cx+2, cy+2, 5, 5, VGA13_BLACK);
        vga13_put_pixel(cx+2, cy+2, VGA13_WHITE);
        vga13_put_pixel(cx+6, cy+2, VGA13_WHITE);
        vga13_put_pixel(cx+2, cy+6, VGA13_WHITE);
        vga13_put_pixel(cx+6, cy+6, VGA13_WHITE);
    }
}

static void draw_checkbox(int x, int y, int checked) {
    int i;
    vga13_fill_rect(x, y, 11, 11, VGA13_WHITE);
    for (i = 0; i < 11; i++) {
        vga13_put_pixel(x+i, y,    VGA13_BLACK);
        vga13_put_pixel(x+i, y+10, VGA13_BLACK);
        vga13_put_pixel(x,   y+i,  VGA13_BLACK);
        vga13_put_pixel(x+10,y+i,  VGA13_BLACK);
    }
    if (checked) {
        vga13_put_pixel(x+2, y+5, VGA13_BLACK);
        vga13_put_pixel(x+2, y+6, VGA13_BLACK);
        vga13_put_pixel(x+3, y+6, VGA13_BLACK);
        vga13_put_pixel(x+3, y+7, VGA13_BLACK);
        vga13_put_pixel(x+4, y+7, VGA13_BLACK);
        vga13_put_pixel(x+4, y+8, VGA13_BLACK);
        vga13_put_pixel(x+5, y+7, VGA13_BLACK);
        vga13_put_pixel(x+6, y+6, VGA13_BLACK);
        vga13_put_pixel(x+7, y+5, VGA13_BLACK);
        vga13_put_pixel(x+8, y+4, VGA13_BLACK);
        vga13_put_pixel(x+8, y+3, VGA13_BLACK);
    }
}

static void hline_cp(int x1, int x2, int y) {
    int x; for (x = x1; x <= x2; x++) vga13_put_pixel(x, y, VGA13_BLACK);
}
static void vline_cp(int x, int y1, int y2) {
    int y; for (y = y1; y <= y2; y++) vga13_put_pixel(x, y, VGA13_BLACK);
}
static void rect_cp(int x, int y, int w, int h) {
    hline_cp(x, x+w-1, y); hline_cp(x, x+w-1, y+h-1);
    vline_cp(x, y, y+h-1); vline_cp(x+w-1, y, y+h-1);
}

static void draw_checker(int x, int y, int w, int h) {
    int px, py;
    for (py = 0; py < h; py++)
        for (px = 0; px < w; px++)
            vga13_put_pixel(x+px, y+py, ((px+py)%2==0) ? VGA13_BLACK : VGA13_WHITE);
}

static void draw_gray_pat(int x, int y, int w, int h) {
    int px, py;
    for (py = 0; py < h; py++)
        for (px = 0; px < w; px++)
            vga13_put_pixel(x+px, y+py, ((px+py)%2==0) ? PAL_LIGHT_GRAY : VGA13_WHITE);
}

static void draw_clock_icon(int x, int y) {
    
    static const int pts[][2] = {
        {2,0},{3,0},{4,0},{5,0},{6,0},
        {1,1},{7,1},{0,2},{8,2},{0,3},{8,3},{0,4},{8,4},
        {0,5},{8,5},{0,6},{8,6},{1,7},{7,7},
        {2,8},{3,8},{4,8},{5,8},{6,8},
    };
    int i, n = (int)(sizeof(pts)/sizeof(pts[0]));
    for (i = 0; i < n; i++)
        vga13_put_pixel(x+pts[i][0], y+pts[i][1], VGA13_BLACK);
    
    vga13_put_pixel(x+4, y+2, VGA13_BLACK);
    vga13_put_pixel(x+4, y+3, VGA13_BLACK);
    vga13_put_pixel(x+4, y+4, VGA13_BLACK);
    vga13_put_pixel(x+5, y+4, VGA13_BLACK);
    vga13_put_pixel(x+6, y+4, VGA13_BLACK);
}

static void draw_cal_icon(int x, int y) {
    int i;
    
    rect_cp(x, y, 13, 13);
    
    for (i = x+1; i < x+12; i++) vga13_put_pixel(i, y+1, VGA13_BLACK);
    
    vga13_put_pixel(x+3, y+4, VGA13_BLACK);
    vga13_put_pixel(x+3, y+5, VGA13_BLACK);
    vga13_put_pixel(x+3, y+6, VGA13_BLACK);
    vga13_put_pixel(x+3, y+7, VGA13_BLACK);
    vga13_put_pixel(x+3, y+8, VGA13_BLACK);
    
    vga13_put_pixel(x+6, y+4, VGA13_BLACK);
    vga13_put_pixel(x+7, y+4, VGA13_BLACK);
    vga13_put_pixel(x+8, y+4, VGA13_BLACK);
    vga13_put_pixel(x+8, y+5, VGA13_BLACK);
    vga13_put_pixel(x+6, y+6, VGA13_BLACK);
    vga13_put_pixel(x+7, y+6, VGA13_BLACK);
    vga13_put_pixel(x+8, y+6, VGA13_BLACK);
    vga13_put_pixel(x+8, y+7, VGA13_BLACK);
    vga13_put_pixel(x+6, y+8, VGA13_BLACK);
    vga13_put_pixel(x+7, y+8, VGA13_BLACK);
    vga13_put_pixel(x+8, y+8, VGA13_BLACK);
}


static void draw_client_control_panel(int id, int cx, int cy, int cw, int ch) {
    (void)id;
    int i;

    vga13_fill_rect(cx, cy, cw, ch, VGA13_WHITE);

    
    int pad   = 5;
    int col1w = (cw * 55) / 100;
    int col2x = cx + col1w;
    int col2w = cw - col1w;

    
    vline_cp(col2x, cy, cy+ch-1);

    
    int lx = cx + pad;
    int ly = cy + pad;

    
    vga13_draw_string(lx, ly, "Desktop Pattern", VGA13_BLACK, VGA13_WHITE, 0);
    ly += 10;

    int pw = (col1w - pad*2 - 8) / 2;
    int ph = 18;
    if (pw < 20) pw = 20;

    
    rect_cp(lx-1,    ly-1, pw+2, ph+2);
    rect_cp(lx-2,    ly-2, pw+4, ph+4);
    draw_checker(lx, ly, pw, ph);

    
    rect_cp(lx+pw+6-1, ly-1, pw+2, ph+2);
    draw_gray_pat(lx+pw+6, ly, pw, ph);

    ly += ph + 6;

    
    int lblw = vga13_draw_string ? 0 : 0;
    (void)lblw;
    {
        int total = 15 * 6;
        int off   = (col1w - pad - total) / 2;
        if (off < 0) off = 0;
        vga13_draw_string(cx + off, ly, "Colour", PAL_DARK_GRAY, VGA13_WHITE, 0);
    }
    ly += 10;

    
    int sq = (col1w - pad*2) / 8;
    if (sq < 8) sq = 8;
    for (i = 0; i < 8; i++) {
        u8 c;
        if      (i == 0) c = VGA13_BLACK;
        else if (i == 7) c = VGA13_WHITE;
        else if (i < 4)  c = PAL_DARK_GRAY;
        else             c = PAL_LIGHT_GRAY;
        vga13_fill_rect(lx + i*(sq+1), ly, sq, sq-1, c);
        rect_cp(lx + i*(sq+1), ly, sq, sq-1);
    }
    ly += sq + 4;

    
    hline_cp(cx+1, col2x-1, ly);
    ly += 5;


    vga13_draw_string(lx, ly, "System Settings", VGA13_BLACK, VGA13_WHITE, 0);
    ly += 10;

    
    int sec_w = col1w - pad*2;
    int sec_h = 46;
    rect_cp(lx-1, ly-1, sec_w+2, sec_h+2);

    
    draw_checkbox(lx+4, ly+4, ui_show_scrollbar_grid);
    vga13_draw_string(lx+20, ly+5, "Scroll Grid", VGA13_BLACK, VGA13_WHITE, 0);

    
    draw_checkbox(lx+4, ly+18, calc_default_scientific);
    vga13_draw_string(lx+20, ly+19, "Sci. Calc", VGA13_BLACK, VGA13_WHITE, 0);

    
    draw_checkbox(lx+4, ly+32, ui_clock_show_seconds);
    vga13_draw_string(lx+20, ly+33, "Clock Sec.", VGA13_BLACK, VGA13_WHITE, 0);

    ly += sec_h + 6;

    
    int rx2 = col2x + pad;
    int ry  = cy + pad;

    
    vga13_draw_string(rx2, ry, "Point Blinking", VGA13_BLACK, VGA13_WHITE, 0);
    ry += 13;

    
    {
        int mid = col2w / 2 - 4;
        int sx  = rx2 + 2;
        int fx  = col2x + col2w - 25;
        draw_radio(sx, ry, 0);
        draw_radio(fx, ry, 1);
        hline_cp(sx+9, fx-1, ry+4);
        ry += 11;
        vga13_draw_string(sx,   ry, "Slow", VGA13_BLACK, VGA13_WHITE, 0);
        vga13_draw_string(fx-2, ry, "Fast", VGA13_BLACK, VGA13_WHITE, 0);
        ry += 12;
        (void)mid;
    }

    hline_cp(col2x+1, col2x+col2w-1, ry);
    ry += 8;

    
    draw_clock_icon(rx2, ry);
    vga13_draw_string(rx2+12, ry+1, "Time", VGA13_BLACK, VGA13_WHITE, 0);
    ry += 12;

    
    {
        rtc_time_t tm;
        rtc_read(&tm);
        char tbuf[20];
        tbuf[0] = '0' + (tm.hour / 10);
        tbuf[1] = '0' + (tm.hour % 10);
        tbuf[2] = ':';
        tbuf[3] = '0' + (tm.minute / 10);
        tbuf[4] = '0' + (tm.minute % 10);
        tbuf[5] = ':';
        tbuf[6] = '0' + (tm.second / 10);
        tbuf[7] = '0' + (tm.second % 10);
        tbuf[8] = 0;
        vga13_draw_string(rx2, ry, tbuf, VGA13_BLACK, VGA13_WHITE, 0);
        ry += 11;
    }

    
    draw_radio(rx2,    ry, 1); vga13_draw_string(rx2+11,    ry+1, "12hr", VGA13_BLACK, VGA13_WHITE, 0);
    draw_radio(rx2+40, ry, 0); vga13_draw_string(rx2+40+11, ry+1, "24hr", VGA13_BLACK, VGA13_WHITE, 0);
    ry += 15;

    hline_cp(col2x+1, col2x+col2w-1, ry);
    ry += 8;

    
    draw_cal_icon(rx2, ry);
    vga13_draw_string(rx2+16, ry+3, "Date", VGA13_BLACK, VGA13_WHITE, 0);
    ry += 17;

    
    vga13_draw_string(rx2, ry, "3/31/26", VGA13_BLACK, VGA13_WHITE, 0);
    ry += 10;
}


static u32 puzzle_rng_state = 0x12345678u;

static u32 puzzle_rand(void) {
    
    puzzle_rng_state ^= puzzle_rng_state << 13;
    puzzle_rng_state ^= puzzle_rng_state >> 17;
    puzzle_rng_state ^= puzzle_rng_state << 5;
    return puzzle_rng_state;
}

static int puzzle_is_solved(int id) {
    if (id < 0 || id >= MAX_WIN) return 0;
    for (int i = 0; i < 15; i++) {
        if (puzzle_tiles[id][i] != i + 1) return 0;
    }
    return puzzle_tiles[id][15] == 0;
}

static void puzzle_init(int id) {
    if (id < 0 || id >= MAX_WIN) return;

    
    for (int i = 0; i < 15; i++) puzzle_tiles[id][i] = i + 1;
    puzzle_tiles[id][15] = 0;
    puzzle_empty_pos[id] = 15;
    puzzle_moves[id] = 0;
    puzzle_solved[id] = 0;

    
    puzzle_rng_state = timer_ticks() ^ ((u32)id * 0x9e3779b9u);
    int steps = 80 + (timer_ticks() % 80);
    int prev_empty = -1;
    while (steps-- > 0) {
        int empty = puzzle_empty_pos[id];
        int er = empty / 4;
        int ec = empty % 4;

        int neigh[4];
        int n = 0;
        if (er > 0) neigh[n++] = empty - 4;
        if (er < 3) neigh[n++] = empty + 4;
        if (ec > 0) neigh[n++] = empty - 1;
        if (ec < 3) neigh[n++] = empty + 1;

        
        if (prev_empty >= 0 && n > 1) {
            int filtered[4];
            int fn = 0;
            for (int i = 0; i < n; i++) {
                if (neigh[i] != prev_empty) filtered[fn++] = neigh[i];
            }
            if (fn > 0) {
                for (int i = 0; i < fn; i++) neigh[i] = filtered[i];
                n = fn;
            }
        }

        int next_empty = neigh[puzzle_rand() % n];

        
        puzzle_tiles[id][empty] = puzzle_tiles[id][next_empty];
        puzzle_tiles[id][next_empty] = 0;
        prev_empty = empty;
        puzzle_empty_pos[id] = next_empty;
    }

    puzzle_solved[id] = puzzle_is_solved(id);
}

static void puzzle_try_move_tile(int id, int tile_idx) {
    if (id < 0 || id >= MAX_WIN) return;
    if (tile_idx < 0 || tile_idx >= 16) return;
    if (puzzle_tiles[id][tile_idx] == 0) return;

    int empty = puzzle_empty_pos[id];
    int er = empty / 4, ec = empty % 4;
    int tr = tile_idx / 4, tc = tile_idx % 4;

    int manhattan = (er > tr ? er - tr : tr - er) + (ec > tc ? ec - tc : tc - ec);
    if (manhattan != 1) return;

    
    puzzle_tiles[id][empty] = puzzle_tiles[id][tile_idx];
    puzzle_tiles[id][tile_idx] = 0;
    puzzle_empty_pos[id] = tile_idx;
    puzzle_moves[id]++;

    puzzle_solved[id] = puzzle_is_solved(id);
}

static void handle_puzzle_key(int id, int key) {
    if (id < 0 || id >= MAX_WIN || wins[id].app != APP_PUZZLE) return;
    int empty = puzzle_empty_pos[id];
    int er = empty / 4, ec = empty % 4;
    int next_empty = -1;

    if (key == KEY_UP && er > 0) next_empty = empty - 4;
    else if (key == KEY_DOWN && er < 3) next_empty = empty + 4;
    else if (key == KEY_LEFT && ec > 0) next_empty = empty - 1;
    else if (key == KEY_RIGHT && ec < 3) next_empty = empty + 1;
    else if (key == KEY_ENTER) {
        puzzle_init(id);
        desktop_needs_full_blit = 1;
        return;
    } else {
        return;
    }

    
    puzzle_tiles[id][empty] = puzzle_tiles[id][next_empty];
    puzzle_tiles[id][next_empty] = 0;
    puzzle_empty_pos[id] = next_empty;
    puzzle_moves[id]++;
    puzzle_solved[id] = puzzle_is_solved(id);
    desktop_needs_full_blit = 1;
}

static void draw_client_puzzle(int id, int cx, int cy, int cw, int ch) {
    
    vga13_fill_rect(cx, cy, cw, ch, VGA13_WHITE);
    for (int py = cy; py < cy + ch; py += 2) {
        for (int px = cx + ((py - cy) & 1); px < cx + cw; px += 2) {
            vga13_put_pixel(px, py, PAL_DARK_GRAY);
        }
    }

    
    char buf[24];
    ksnprintf(buf, sizeof(buf), "Moves: %u", (unsigned)puzzle_moves[id]);
    int moves_w = str_len(buf) * 6;
    int moves_x = cx + (cw - moves_w) / 2;
    vga13_draw_string(moves_x, cy + 8, buf, VGA13_BLACK, VGA13_WHITE, 0);

    
    if (puzzle_solved[id]) {
        const char *sol = "Solved";
        int sol_w = str_len(sol) * 6;
        int sol_x = cx + cw - sol_w - 8;
        if (sol_x < moves_x + moves_w + 4) sol_x = moves_x + moves_w + 4;
        if (sol_x + sol_w > cx + cw - 2) sol_x = cx + cw - sol_w - 2;
        vga13_draw_string(sol_x, cy + 8, sol, VGA13_BLACK, VGA13_WHITE, 0);
    }

    
    int header_h = 8 + 8 + 8; 
    int board_max_h = ch - header_h - 8 - 12 - 8; 
    if (board_max_h < 28) return;
    int board_size = (cw < board_max_h) ? cw : board_max_h;
    if (board_size > cw - 16) board_size = cw - 16;
    board_size -= board_size % 4;
    if (board_size < 32) return;

    int tile = board_size / 4;
    int bx = cx + (cw - board_size) / 2;
    int by = cy + header_h;

    
    vga13_fill_rect(bx, by, board_size, board_size, VGA13_WHITE);
    hline_px(bx, bx + board_size - 1, by, VGA13_BLACK);
    hline_px(bx, bx + board_size - 1, by + board_size - 1, VGA13_BLACK);
    vline_px(bx, by, by + board_size - 1, VGA13_BLACK);
    vline_px(bx + board_size - 1, by, by + board_size - 1, VGA13_BLACK);

    
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            int idx = r * 4 + c;
            int val = puzzle_tiles[id][idx];

            int tx = bx + c * tile;
            int ty = by + r * tile;

            if (val == 0) {
                
                vga13_fill_rect(tx + 1, ty + 1, tile - 2, tile - 2, VGA13_WHITE);
                continue;
            }

            u8 tile_bg = puzzle_solved[id] ? PAL_DARK_GRAY : VGA13_WHITE;
            u8 tile_fg = puzzle_solved[id] ? VGA13_WHITE : VGA13_BLACK;

            vga13_fill_rect(tx + 1, ty + 1, tile - 2, tile - 2, tile_bg);
            hline_px(tx + 1, tx + tile - 2, ty + 1, VGA13_BLACK);
            hline_px(tx + 1, tx + tile - 2, ty + tile - 2, VGA13_BLACK);
            vline_px(tx + 1, ty + 1, ty + tile - 2, VGA13_BLACK);
            vline_px(tx + tile - 2, ty + 1, ty + tile - 2, VGA13_BLACK);

            char num[3];
            ksnprintf(num, sizeof(num), "%d", val);
            int nw = str_len(num) * 6;
            int nx = tx + (tile - nw) / 2;
            int ny = ty + (tile - 7) / 2;
            if (nx < tx + 1) nx = tx + 1;
            if (ny < ty + 1) ny = ty + 1;
            vga13_draw_string(nx, ny, num, tile_fg, tile_bg, 0);
        }
    }

    
    
    int btn_w = board_size;
    int btn_h = 12;
    int btn_x = bx; 
    int btn_y = by + board_size + 8; 
    
    
    vga13_fill_rect(btn_x + 1, btn_y + btn_h, btn_w - 1, 1, PAL_DARK_GRAY);
    vga13_fill_rect(btn_x + btn_w, btn_y + 1, 1, btn_h - 1, PAL_DARK_GRAY);
    vga13_put_pixel(btn_x + btn_w, btn_y + btn_h, PAL_DARK_GRAY);

    
    vga13_fill_rect(btn_x, btn_y, btn_w, btn_h, VGA13_WHITE);

    
    hline_px(btn_x, btn_x + btn_w - 1, btn_y, VGA13_BLACK);
    hline_px(btn_x, btn_x + btn_w - 1, btn_y + btn_h - 1, VGA13_BLACK);
    vline_px(btn_x, btn_y, btn_y + btn_h - 1, VGA13_BLACK);
    vline_px(btn_x + btn_w - 1, btn_y, btn_y + btn_h - 1, VGA13_BLACK);

    
    int label_len = 8; 
    int label_x = btn_x + (btn_w - label_len * 6) / 2;
    int label_y = btn_y + (btn_h - 8) / 2;
    if (label_y < btn_y + 2) label_y = btn_y + 2;
    vga13_draw_string(label_x, label_y, "New Game", VGA13_BLACK, VGA13_WHITE, 0);
}

static void draw_client_notepad(int id, int cx, int cy, int cw, int ch) {
    int y;
    int scrollbar_w = 11;
    int content_w = cw - scrollbar_w - 2;
    int content_h = ch - 4;
    int visible_lines = content_h / 8;
    int char_width = 6;
    int chars_per_line = (content_w - 4) / char_width;  
    int scroll_x = cx + cw - scrollbar_w - 1;
    int scroll_y = cy + 2;
    int scroll_h = content_h;
    int thumb_h, thumb_y;
    int buf_pos = 0;
    int current_line = 0;

    if (id < 0 || id >= MAX_WIN) return;

    
    vga13_fill_rect(cx, cy, cw - scrollbar_w - 1, ch, VGA13_WHITE);

    
    y = cy + 4;
    buf_pos = 0;
    current_line = 0;
    while (buf_pos < notepad_buf_len[id] && y + 7 < cy + ch - 2) {
        int line_len = 0;
        char line_buf[128];
        int line_start_pos = buf_pos;

        
        while (buf_pos < notepad_buf_len[id] &&
               notepad_buf[id][buf_pos] != '\n' &&
               line_len < chars_per_line &&
               line_len < 127) {
            line_buf[line_len++] = notepad_buf[id][buf_pos++];
        }
        line_buf[line_len] = 0;

        
        if (buf_pos < notepad_buf_len[id] && notepad_buf[id][buf_pos] == '\n') {
            buf_pos++;
        }

        
        if (current_line >= notepad_scroll_y[id]) {
            int sel_start = notepad_sel_start[id];
            int sel_end = notepad_sel_end[id];
            int line_end_pos = line_start_pos + line_len;
            
            
            if (notepad_has_selection[id] && sel_start < line_end_pos && sel_end > line_start_pos) {
                
                int sel_in_line_start = (sel_start > line_start_pos) ? sel_start - line_start_pos : 0;
                int sel_in_line_end = (sel_end < line_end_pos) ? sel_end - line_start_pos : line_len;
                
                
                if (sel_in_line_start > 0) {
                    char before_sel[128];
                    int i;
                    for (i = 0; i < sel_in_line_start && i < line_len; i++) {
                        before_sel[i] = line_buf[i];
                    }
                    before_sel[i] = 0;
                    vga13_draw_string(cx + 4, y, before_sel, VGA13_BLACK, VGA13_WHITE, 0);
                }
                
                
                if (sel_in_line_end > sel_in_line_start) {
                    char selected[128];
                    int i, j = 0;
                    for (i = sel_in_line_start; i < sel_in_line_end && i < line_len; i++) {
                        selected[j++] = line_buf[i];
                    }
                    selected[j] = 0;
                    
                    vga13_fill_rect(cx + 4 + sel_in_line_start * char_width, y - 1, 
                                   j * char_width, 9, VGA13_BLACK);
                    vga13_draw_string(cx + 4 + sel_in_line_start * char_width, y, 
                                     selected, VGA13_WHITE, VGA13_BLACK, 0);
                }
                
                
                if (sel_in_line_end < line_len) {
                    char after_sel[128];
                    int i, j = 0;
                    for (i = sel_in_line_end; i < line_len; i++) {
                        after_sel[j++] = line_buf[i];
                    }
                    after_sel[j] = 0;
                    vga13_draw_string(cx + 4 + sel_in_line_end * char_width, y, 
                                     after_sel, VGA13_BLACK, VGA13_WHITE, 0);
                }
            } else {
                
                vga13_draw_string(cx + 4, y, line_buf, VGA13_BLACK, VGA13_WHITE, 0);
            }
            y += 8;
        }
        current_line++;
    }

    
    vga13_fill_rect(scroll_x, scroll_y, scrollbar_w, scroll_h, PAL_LIGHT_GRAY);

    if (ui_show_scrollbar_grid) {
        
        for (int gx = scroll_x + 2; gx <= scroll_x + scrollbar_w - 3; gx += 2) {
            vline_px(gx, scroll_y + 2, scroll_y + scroll_h - 3, PAL_DARK_GRAY);
        }
        for (int gy = scroll_y + 2; gy <= scroll_y + scroll_h - 3; gy += 4) {
            hline_px(scroll_x + 2, scroll_x + scrollbar_w - 3, gy, PAL_DARK_GRAY);
        }
    }

    hline_px(scroll_x, scroll_x + scrollbar_w - 1, scroll_y, VGA13_BLACK);
    hline_px(scroll_x, scroll_x + scrollbar_w - 1, scroll_y + scroll_h - 1, VGA13_BLACK);
    vline_px(scroll_x, scroll_y, scroll_y + scroll_h - 1, VGA13_BLACK);
    vline_px(scroll_x + scrollbar_w - 1, scroll_y, scroll_y + scroll_h - 1, VGA13_BLACK);

    
    vga13_fill_rect(scroll_x + 1, scroll_y + 1, scrollbar_w - 2, 10, VGA13_WHITE);
    hline_px(scroll_x + 1, scroll_x + scrollbar_w - 2, scroll_y + 1, VGA13_BLACK);
    hline_px(scroll_x + 1, scroll_x + scrollbar_w - 2, scroll_y + 10, VGA13_BLACK);
    vline_px(scroll_x + 1, scroll_y + 1, scroll_y + 10, VGA13_BLACK);
    vline_px(scroll_x + scrollbar_w - 2, scroll_y + 1, scroll_y + 10, VGA13_BLACK);
    vga13_draw_string(scroll_x + 3, scroll_y + 2, "\x1E", VGA13_BLACK, VGA13_WHITE, 0);

    
    vga13_fill_rect(scroll_x + 1, scroll_y + scroll_h - 11, scrollbar_w - 2, 10, VGA13_WHITE);
    hline_px(scroll_x + 1, scroll_x + scrollbar_w - 2, scroll_y + scroll_h - 11, VGA13_BLACK);
    hline_px(scroll_x + 1, scroll_x + scrollbar_w - 2, scroll_y + scroll_h - 1, VGA13_BLACK);
    vline_px(scroll_x + 1, scroll_y + scroll_h - 11, scroll_y + scroll_h - 1, VGA13_BLACK);
    vline_px(scroll_x + scrollbar_w - 2, scroll_y + scroll_h - 11, scroll_y + scroll_h - 1, VGA13_BLACK);
    vga13_draw_string(scroll_x + 3, scroll_y + scroll_h - 10, "\x1F", VGA13_BLACK, VGA13_WHITE, 0);

    
    {
        int total_lines = 1;
        int p;
        int line_len = 0;
        for (p = 0; p < notepad_buf_len[id]; p++) {
            if (notepad_buf[id][p] == '\n') {
                total_lines++;
                line_len = 0;
            } else {
                line_len++;
                if (line_len >= chars_per_line) {
                    total_lines++;
                    line_len = 0;
                }
            }
        }
        notepad_total_lines[id] = total_lines;

        
        if (total_lines <= visible_lines) {
            thumb_h = scroll_h - 26;  
            thumb_y = scroll_y + 13;
        } else {
            thumb_h = (visible_lines * (scroll_h - 26)) / total_lines;
            if (thumb_h < 20) thumb_h = 20;  
            thumb_y = scroll_y + 13 + (notepad_scroll_y[id] * (scroll_h - 26 - thumb_h)) /
                      (total_lines - visible_lines);
        }
    }

    
    vga13_fill_rect(scroll_x + 2, thumb_y, scrollbar_w - 4, thumb_h, PAL_LIGHT_GRAY);
    hline_px(scroll_x + 2, scroll_x + scrollbar_w - 3, thumb_y, VGA13_BLACK);
    hline_px(scroll_x + 2, scroll_x + scrollbar_w - 3, thumb_y + thumb_h - 1, VGA13_BLACK);
    vline_px(scroll_x + 2, thumb_y, thumb_y + thumb_h - 1, VGA13_BLACK);
    vline_px(scroll_x + scrollbar_w - 3, thumb_y, thumb_y + thumb_h - 1, VGA13_BLACK);
    hline_px(scroll_x + 3, scroll_x + scrollbar_w - 4, thumb_y + 1, VGA13_WHITE);
    vline_px(scroll_x + 3, thumb_y + 1, thumb_y + thumb_h - 2, VGA13_WHITE);

    
    if (win_top_id() == id) {
        int cursor_line = 0;
        int cursor_col = 0;
        int line_start = 0;
        int chars_per_line = (content_w - 4) / char_width;  
        int p = 0;

        
        while (p < notepad_cursor_pos[id] && p < notepad_buf_len[id]) {
            
            if (notepad_buf[id][p] == '\n') {
                cursor_line++;
                cursor_col = 0;
                line_start = p + 1;
            } else {
                
                int col_in_line = p - line_start;
                if (col_in_line >= chars_per_line) {
                    cursor_line++;
                    cursor_col = 0;
                    line_start = p;
                } else {
                    cursor_col = col_in_line + 1;  
                }
            }
            p++;
        }

        
        if (notepad_cursor_pos[id] == 0 || 
            (notepad_cursor_pos[id] > 0 && notepad_buf[id][notepad_cursor_pos[id] - 1] == '\n')) {
            cursor_col = 0;
        }

        notepad_cursor_y[id] = cursor_line;
        notepad_cursor_x[id] = cursor_col;

        
        if (cursor_line >= notepad_scroll_y[id] &&
            cursor_line < notepad_scroll_y[id] + visible_lines) {
            int cursor_screen_y = cy + 4 + (cursor_line - notepad_scroll_y[id]) * 8;
            int cursor_screen_x = cx + 4 + cursor_col * char_width;
            vline_px(cursor_screen_x, cursor_screen_y, cursor_screen_y + 7, VGA13_BLACK);
        }

        
        if (notepad_scrollbar_dragging < 0 && notepad_manual_scroll_timer == 0) {
            if (cursor_line < notepad_scroll_y[id]) {
                notepad_scroll_y[id] = cursor_line;
                desktop_needs_full_blit = 1;
            } else if (cursor_line >= notepad_scroll_y[id] + visible_lines) {
                notepad_scroll_y[id] = cursor_line - visible_lines + 1;
                desktop_needs_full_blit = 1;
            }
        }
    }
}

static void draw_client_calc(int id, int cx, int cy, int cw, int ch) {
    
    int btn_w = 16;
    int btn_h = 12;
    int gap = 3;
    int display_h = 16;
    int margin = 6;
    int start_x = cx + margin;
    int start_y = cy + margin;
    int i, r, c;
    char display_buf[16];
    int display_len;
    
    
    int btn_area_y = start_y + display_h + 6;
    
    
    int total_btn_w = 4 * btn_w + 3 * gap;
    int total_btn_h = 5 * btn_h + 4 * gap;

    
    vga13_fill_rect(cx, cy, cw, ch, VGA13_WHITE);
    for (int py = cy; py < cy + ch; py += 2) {
        for (int px = cx + ((py - cy) & 1); px < cx + cw; px += 2) {
            vga13_put_pixel(px, py, PAL_DARK_GRAY);
        }
    }

    
    int disp_w = total_btn_w;
    vga13_fill_rect(start_x, start_y, disp_w, display_h, VGA13_WHITE);
    hline_px(start_x, start_x + disp_w - 1, start_y, VGA13_BLACK);
    hline_px(start_x, start_x + disp_w - 1, start_y + display_h - 1, VGA13_BLACK);
    vline_px(start_x, start_y, start_y + display_h - 1, VGA13_BLACK);
    vline_px(start_x + disp_w - 1, start_y, start_y + display_h - 1, VGA13_BLACK);

    
    if (id >= 0 && id < MAX_WIN) {
        display_len = str_len(calc_display[id]);
        if (display_len > 11) display_len = 11;
        for (i = 0; i < display_len; i++) display_buf[i] = calc_display[id][i];
        display_buf[i] = 0;
    } else {
        display_buf[0] = '0'; display_buf[1] = 0;
    }
    int text_w = str_len(display_buf) * 6;
    int text_x = start_x + disp_w - 4 - text_w;
    if (text_x < start_x + 2) text_x = start_x + 2;
    vga13_draw_string(text_x, start_y + 5, display_buf, VGA13_BLACK, VGA13_WHITE, 0);

    
    const char *btn_labels[20] = {
        "C", "E", "=", "*",
        "7", "8", "9", "/",
        "4", "5", "6", "-",
        "1", "2", "3", "+",
        "0", "", ".",  ""
    };

    
    for (r = 0; r < 5; r++) {
        for (c = 0; c < 4; c++) {
            int bx = start_x + c * (btn_w + gap);
            int by = btn_area_y + r * (btn_h + gap);
            int bw = btn_w;
            int bh = btn_h;
            char label[2] = { btn_labels[r*4+c][0], 0 };

            
            if (r == 4 && c == 0) {
                bw = btn_w * 2 + gap;
            }
            
            else if (r == 4 && c == 1) {
                continue;
            }
            
            else if (r == 3 && c == 3) {
                bh = btn_h * 2 + gap;
            }
            
            else if (r == 4 && c == 3) {
                continue;
            }
            
            else if (btn_labels[r*4+c][0] == 0) {
                continue;
            }

            
            vga13_fill_rect(bx + 1, by + bh, bw - 1, 1, PAL_DARK_GRAY);
            vga13_fill_rect(bx + bw, by + 1, 1, bh - 1, PAL_DARK_GRAY);
            vga13_put_pixel(bx + bw, by + bh, PAL_DARK_GRAY);

            
            vga13_fill_rect(bx, by, bw, bh, VGA13_WHITE);
            
            
            hline_px(bx, bx + bw - 1, by, VGA13_BLACK);
            hline_px(bx, bx + bw - 1, by + bh - 1, VGA13_BLACK);
            vline_px(bx, by, by + bh - 1, VGA13_BLACK);
            vline_px(bx + bw - 1, by, by + bh - 1, VGA13_BLACK);

            
            int label_x = bx + (bw - 6) / 2;
            int label_y = by + (bh - 8) / 2;
            if (label_y < by + 2) label_y = by + 2;
            vga13_draw_string(label_x, label_y, label, VGA13_BLACK, VGA13_WHITE, 0);
        }
    }
}

static void draw_client_terminal(int id, int cx, int cy, int cw, int ch) {
    int i, y;
    int scrollbar_w = 11;
    int content_w = cw - scrollbar_w - 2;
    int content_h = ch - 4;
    int visible_lines = content_h / 8;
    int char_width = 6;
    int chars_per_line = (content_w - 4) / char_width;
    int scroll_x = cx + cw - scrollbar_w - 1;
    int scroll_y = cy + 2;
    int scroll_h = content_h;
    int thumb_h, thumb_y;
    int total_wrapped_lines = 0;
    int current_line = 0;
    int line_count = term_get_line_count(id);
    int scroll_y_val = term_get_scroll_y(id);
    const char *input = term_get_input(id);
    int input_len = term_get_input_len(id);
    
    if (id < 0 || id >= MAX_WIN) {
        vga13_fill_rect(cx, cy, cw, ch, VGA13_BLACK);
        vga13_draw_string(cx + 6, cy + 8, "c>_", VGA13_WHITE, VGA13_BLACK, 0);
        return;
    }
    
    
    for (i = 0; i < line_count; i++) {
        int line_len = str_len(term_get_line(id, i));
        int wrapped = (line_len + chars_per_line - 1) / chars_per_line;
        if (wrapped < 1) wrapped = 1;
        total_wrapped_lines += wrapped;
    }
    if (total_wrapped_lines < 1) total_wrapped_lines = 1;
    
    
    if (scroll_y_val < 0) scroll_y_val = 0;
    if (scroll_y_val > total_wrapped_lines - visible_lines)
        scroll_y_val = total_wrapped_lines - visible_lines;
    if (scroll_y_val < 0) scroll_y_val = 0;
    
    
    vga13_fill_rect(cx, cy, cw - scrollbar_w - 1, ch, VGA13_BLACK);
    
    
    y = cy + 4;
    current_line = 0;
    for (i = 0; i < line_count; i++) {
        const char *line = term_get_line(id, i);
        int line_len = str_len(line);
        int pos = 0;
        int line_wrapped = 0;
        
        
        if (line_len == 0) {
            line_wrapped = 1;
        } else {
            line_wrapped = (line_len + chars_per_line - 1) / chars_per_line;
            if (line_wrapped < 1) line_wrapped = 1;
        }
        
        
        if (current_line + line_wrapped <= scroll_y_val) {
            
            current_line += line_wrapped;
            continue;
        }
        
        
        if (y + 7 >= cy + ch - 8) break;
        
        
        while (pos < line_len) {
            int visual_line_idx = current_line + (pos / chars_per_line);
            
            
            if (visual_line_idx < scroll_y_val) {
                pos += chars_per_line;
                continue;
            }
            
            
            if (y + 7 >= cy + ch - 8) break;
            
            
            int segment_len = chars_per_line;
            if (pos + segment_len > line_len) segment_len = line_len - pos;
            if (segment_len > 0) {
                char segment[128];
                int j;
                for (j = 0; j < segment_len && j < 127; j++)
                    segment[j] = line[pos + j];
                segment[j] = 0;
                vga13_draw_string(cx + 4, y, segment, VGA13_WHITE, VGA13_BLACK, 0);
            }
            y += 8;
            pos += chars_per_line;
        }
        
        
        if (line_len == 0) {
            if (current_line >= scroll_y_val && y + 7 < cy + ch - 8) {
                y += 8;
            }
            current_line++;
        } else {
            current_line += line_wrapped;
        }
    }
    
    
    {
        int prompt_y = cy + ch - 10;
        int input_chars_per_line = chars_per_line - 2; 
        int input_pos = 0;
        
        
        int input_wrapped = (input_len + input_chars_per_line - 1) / input_chars_per_line;
        if (input_wrapped < 1) input_wrapped = 1;
        
        
        prompt_y = cy + ch - 10 - (input_wrapped - 1) * 8;
        if (prompt_y < cy + 4) prompt_y = cy + 4;
        
        
        vga13_draw_string(cx + 4, prompt_y, "> ", VGA13_WHITE, VGA13_BLACK, 0);
        
        
        while (input_pos < input_len && prompt_y < cy + ch - 2) {
            int segment_len = input_chars_per_line;
            if (input_pos + segment_len > input_len) segment_len = input_len - input_pos;
            if (segment_len > 0) {
                char segment[128];
                int j;
                for (j = 0; j < segment_len && j < 127; j++)
                    segment[j] = input[input_pos + j];
                segment[j] = 0;
                
                int x_offset = (input_pos == 0) ? 2 * char_width : 0;
                vga13_draw_string(cx + 4 + x_offset, prompt_y, segment, VGA13_WHITE, VGA13_BLACK, 0);
            }
            input_pos += input_chars_per_line;
            prompt_y += 8;
        }
    }
    
    
    vga13_fill_rect(scroll_x, scroll_y, scrollbar_w, scroll_h, PAL_LIGHT_GRAY);

    if (ui_show_scrollbar_grid) {
        
        for (int gx = scroll_x + 2; gx <= scroll_x + scrollbar_w - 3; gx += 2) {
            vline_px(gx, scroll_y + 2, scroll_y + scroll_h - 3, PAL_DARK_GRAY);
        }
        for (int gy = scroll_y + 2; gy <= scroll_y + scroll_h - 3; gy += 4) {
            hline_px(scroll_x + 2, scroll_x + scrollbar_w - 3, gy, PAL_DARK_GRAY);
        }
    }

    hline_px(scroll_x, scroll_x + scrollbar_w - 1, scroll_y, VGA13_BLACK);
    hline_px(scroll_x, scroll_x + scrollbar_w - 1, scroll_y + scroll_h - 1, VGA13_BLACK);
    vline_px(scroll_x, scroll_y, scroll_y + scroll_h - 1, VGA13_BLACK);
    vline_px(scroll_x + scrollbar_w - 1, scroll_y, scroll_y + scroll_h - 1, VGA13_BLACK);
    
    
    vga13_fill_rect(scroll_x + 1, scroll_y + 1, scrollbar_w - 2, 10, VGA13_WHITE);
    hline_px(scroll_x + 1, scroll_x + scrollbar_w - 2, scroll_y + 1, VGA13_BLACK);
    hline_px(scroll_x + 1, scroll_x + scrollbar_w - 2, scroll_y + 10, VGA13_BLACK);
    vline_px(scroll_x + 1, scroll_y + 1, scroll_y + 10, VGA13_BLACK);
    vline_px(scroll_x + scrollbar_w - 2, scroll_y + 1, scroll_y + 10, VGA13_BLACK);
    vga13_draw_string(scroll_x + 3, scroll_y + 2, "\x1E", VGA13_BLACK, VGA13_WHITE, 0);
    
    
    vga13_fill_rect(scroll_x + 1, scroll_y + scroll_h - 11, scrollbar_w - 2, 10, VGA13_WHITE);
    hline_px(scroll_x + 1, scroll_x + scrollbar_w - 2, scroll_y + scroll_h - 11, VGA13_BLACK);
    hline_px(scroll_x + 1, scroll_x + scrollbar_w - 2, scroll_y + scroll_h - 1, VGA13_BLACK);
    vline_px(scroll_x + 1, scroll_y + scroll_h - 11, scroll_y + scroll_h - 1, VGA13_BLACK);
    vline_px(scroll_x + scrollbar_w - 2, scroll_y + scroll_h - 11, scroll_y + scroll_h - 1, VGA13_BLACK);
    vga13_draw_string(scroll_x + 3, scroll_y + scroll_h - 10, "\x1F", VGA13_BLACK, VGA13_WHITE, 0);
    
    
    if (total_wrapped_lines <= visible_lines) {
        thumb_h = scroll_h - 22;
        thumb_y = scroll_y + 11;
    } else {
        thumb_h = (visible_lines * (scroll_h - 22)) / total_wrapped_lines;
        if (thumb_h < 16) thumb_h = 16;
        thumb_y = scroll_y + 11 + (scroll_y_val * (scroll_h - 22 - thumb_h)) /
                  (total_wrapped_lines - visible_lines);
    }
    
    
    vga13_fill_rect(scroll_x + 2, thumb_y, scrollbar_w - 4, thumb_h, PAL_LIGHT_GRAY);
    hline_px(scroll_x + 2, scroll_x + scrollbar_w - 3, thumb_y, VGA13_BLACK);
    hline_px(scroll_x + 2, scroll_x + scrollbar_w - 3, thumb_y + thumb_h - 1, VGA13_BLACK);
    vline_px(scroll_x + 2, thumb_y, thumb_y + thumb_h - 1, VGA13_BLACK);
    vline_px(scroll_x + scrollbar_w - 3, thumb_y, thumb_y + thumb_h - 1, VGA13_BLACK);
    hline_px(scroll_x + 3, scroll_x + scrollbar_w - 4, thumb_y + 1, VGA13_WHITE);
    vline_px(scroll_x + 3, thumb_y + 1, thumb_y + thumb_h - 2, VGA13_WHITE);
}

static void draw_client_trash(int cx, int cy, int cw, int ch) {
    vga13_fill_rect(cx, cy, cw, ch, VGA13_WHITE);
    vga13_draw_string(cx + 6, cy + 8, "Trash is empty.", VGA13_BLACK, VGA13_WHITE, 0);
}

static void draw_client_disk(int id, int cx, int cy, int cw, int ch) {
    int i, rows, y, name_w, type_x;
    vga13_fill_rect(cx, cy, cw, ch, VGA13_WHITE);
    vga13_fill_rect(cx, cy, cw, 10, PAL_LIGHT_GRAY);
    
    
    name_w = (cw - 20) / 2;
    type_x = cx + name_w + 8;
    
    vga13_draw_string(cx + 4, cy + 2, "Name", VGA13_BLACK, PAL_LIGHT_GRAY, 0);
    vga13_draw_string(type_x, cy + 2, "Type", VGA13_BLACK, PAL_LIGHT_GRAY, 0);
    vga13_draw_string(cx + cw - 40, cy + 2, "Size", VGA13_BLACK, PAL_LIGHT_GRAY, 0);

    
    if (fs_using_fat32()) {
        vga13_draw_string(cx + 4, cy + 2, "[FAT32]", VGA13_BLACK, PAL_LIGHT_GRAY, 0);
    } else {
        vga13_draw_string(cx + 4, cy + 2, "[RAM]", VGA13_BLACK, PAL_LIGHT_GRAY, 0);
    }

    
    if (id >= 0 && id < MAX_WIN) {
        disk_count[id] = fs_list_dir(disk_cwd_cluster[id], disk_entries[id], FS_MAX_FILES);
    } else {
        disk_count[id] = 0;
    }
    if (disk_sel[id] >= disk_count[id]) disk_sel[id] = disk_count[id] - 1;
    if (disk_sel[id] < 0) disk_sel[id] = 0;

    rows = (ch - 24) / 8;
    if (rows < 1) rows = 1;
    y = cy + 12;
    
    for (i = 0; i < disk_count[id] && i < rows; i++) {
        int inv = (i == disk_sel[id]);
        u32 sz = disk_entries[id][i].size;
        char size_buf[12];
        char type_buf[5] = "    ";
        int p = 0;
        
        
        if (disk_entries[id][i].is_dir) {
            type_buf[0] = 'f';
            type_buf[1] = 'o';
            type_buf[2] = 'l';
            type_buf[3] = 'd';
        } else {
            
            char *ext = 0;
            int j;
            for (j = 0; disk_entries[id][i].name[j]; j++) {
                if (disk_entries[id][i].name[j] == '.') ext = (char*)&disk_entries[id][i].name[j+1];
            }
            if (ext && ext[0] && ext[1] && ext[2]) {
                type_buf[0] = (char)toupper(ext[0]);
                type_buf[1] = (char)toupper(ext[1]);
                type_buf[2] = (char)toupper(ext[2]);
                type_buf[3] = ' ';
            } else {
                type_buf[0] = 'T';
                type_buf[1] = 'E';
                type_buf[2] = 'X';
                type_buf[3] = 'T';
            }
        }
        
        if (inv) vga13_fill_rect(cx + 2, y - 1, cw - 4, 8, VGA13_BLACK);
        
        
        if (sz == 0) size_buf[p++] = '0';
        else {
            char rev[12];
            int r = 0;
            while (sz && r < 10) { rev[r++] = (char)('0' + (sz % 10)); sz /= 10; }
            while (r--) size_buf[p++] = rev[r];
        }
        size_buf[p++] = 'b';
        size_buf[p] = 0;
        
        
        vga13_draw_string(cx + 4, y, disk_entries[id][i].name, VGA13_BLACK, VGA13_WHITE, inv);
        vga13_draw_string(type_x, y, type_buf, VGA13_BLACK, VGA13_WHITE, inv);
        vga13_draw_string(cx + cw - 40, y, size_buf, VGA13_BLACK, VGA13_WHITE, inv);
        
        y += 8;
    }
}

static void draw_client(int id) {
    int cx, cy, cw, ch;
    if (id < 0 || !wins[id].used) return;
    cx = wins[id].x + 3;
    cy = wins[id].y + SVS_TITLE_H + 1;
    cw = wins[id].w - 6;
    ch = wins[id].h - SVS_TITLE_H - 3;
    if (cw < 8 || ch < 8) return;

    switch (wins[id].app) {
    case APP_ABOUT: draw_client_about(cx, cy, cw, ch); break;
    case APP_CONTROL_PANEL: draw_client_control_panel(id, cx, cy, cw, ch); break;
    case APP_PUZZLE: draw_client_puzzle(id, cx, cy, cw, ch); break;
    case APP_NOTEPAD: draw_client_notepad(id, cx, cy, cw, ch); break;
    case APP_CALC: draw_client_calc(id, cx, cy, cw, ch); break;
    case APP_TERMINAL: draw_client_terminal(id, cx, cy, cw, ch); break;
    case APP_DISK: draw_client_disk(id, cx, cy, cw, ch); break;
    case APP_TRASH: draw_client_trash(cx, cy, cw, ch); break;
    default: vga13_fill_rect(cx, cy, cw, ch, VGA13_WHITE); break;
    }
}

static void draw_one_window(int id) {
    int active_id;
    if (id < 0 || !wins[id].used) return;
    active_id = win_top_id();
    draw_svs_window(wins[id].x, wins[id].y, wins[id].w, wins[id].h, wins[id].title, id == active_id);
    draw_client(id);
}

static void draw_windows_bottom_to_top(void) {
    int zi, i, zm = win_max_z();
    if (zm < 1) zm = 1;
    for (zi = 1; zi <= zm; zi++) {
        for (i = 0; i < MAX_WIN; i++) {
            if (wins[i].used && wins[i].z == zi && !wins[i].animating) draw_one_window(i);
        }
    }
}

static int any_window_animating(void) {
    int i;
    for (i = 0; i < MAX_WIN; i++) {
        if (wins[i].used && wins[i].animating) return 1;
    }
    return 0;
}

static void draw_animating_windows(void) {
    int i;
    for (i = 0; i < MAX_WIN; i++) {
        if (wins[i].used && wins[i].animating) {
            animate_window_zoom(i);
            desktop_needs_full_blit = 1;
        }
    }
}

static void draw_desktop_background(void) {
    int x, y;
    int bg_y0 = SVS_MENU_BAR_H + 1;
    vga13_fill_rect(0, bg_y0, VGA13_WIDTH, VGA13_HEIGHT - bg_y0, PAL_DESKTOP);
    for (y = bg_y0; y < VGA13_HEIGHT; y++) {
        for (x = 0; x < VGA13_WIDTH; x++) {
            if (((x ^ y) & 1) == 0) vga13_put_pixel(x, y, PAL_DITHER_A);
        }
    }
}


static const char trash_icon_bmp[16][17] = {
    "                ",
    "     XXXXXX     ",
    "    X......X    ",
    "   XXXXXXXXXX   ",
    "  X..........X  ",
    "  XXXXXXXXXXXX  ",
    "   X.X.XX.X.X   ",
    "   X.X.XX.X.X   ",
    "   X.X.XX.X.X   ",
    "   X.X.XX.X.X   ",
    "   X.X.XX.X.X   ",
    "   X.X.XX.X.X   ",
    "   X.X.XX.X.X   ",
    "    XXXXXXXX    ",
    "                ",
    "                "
};

static void draw_trash_icon(int x, int y, int selected) {
    int r, c;
    for (r = 0; r < 16; r++) {
        for (c = 0; c < 16; c++) {
            char ch = trash_icon_bmp[r][c];
            int px = x + c, py = y + r;
            if (px < 0 || px >= VGA13_WIDTH || py < 0 || py >= VGA13_HEIGHT) continue;
            if (ch == 'X') vga13_put_pixel(px, py, VGA13_BLACK);
            else if (ch == '.') vga13_put_pixel(px, py, VGA13_WHITE);
            else if (ch == 'g') vga13_put_pixel(px, py, PAL_LIGHT_GRAY);
        }
    }
    if (selected) {
        for (r = 0; r < 16; r++) {
            for (c = 0; c < 16; c++) {
                if (((x + c + y + r) & 1) == 0)
                    vga13_put_pixel(x + c, y + r, PAL_DARK_GRAY);
            }
        }
    }
}

static void draw_icon_bitmap(int x, int y, const icon_t* ic) {
    int i;
    vga13_fill_rect(x, y, ICON_SIZE, ICON_SIZE, PAL_DITHER_A);
    vga13_fill_rect(x + 4, y + 3, 16, 18, VGA13_WHITE);
    hline_px(x + 4, x + 19, y + 3, VGA13_BLACK);
    hline_px(x + 4, x + 19, y + 20, VGA13_BLACK);
    vline_px(x + 4, y + 3, y + 20, VGA13_BLACK);
    vline_px(x + 19, y + 3, y + 20, VGA13_BLACK);
    if (!ic) return;
    if (ic->app == APP_NONE) {
        
        if (ic->is_dir) {
            
            vga13_fill_rect(x + 6, y + 9, 12, 9, PAL_LIGHT_GRAY);
            vga13_fill_rect(x + 7, y + 7, 7, 3, PAL_LIGHT_GRAY);
            hline_px(x + 6, x + 17, y + 9, VGA13_BLACK);
            hline_px(x + 6, x + 17, y + 17, VGA13_BLACK);
            vline_px(x + 6, y + 9, y + 17, VGA13_BLACK);
            vline_px(x + 17, y + 9, y + 17, VGA13_BLACK);
        } else {
            
            for (i = 0; i < 5; i++) hline_px(x + 7, x + 16, y + 6 + i * 3, PAL_LIGHT_GRAY);
        }
        return;
    }

    if (ic->app == APP_NOTEPAD) {
        for (i = 0; i < 6; i++) hline_px(x + 7, x + 16, y + 6 + i * 2, PAL_LIGHT_GRAY);
    } else if (ic->app == APP_CALC) {
        vga13_fill_rect(x + 7, y + 6, 10, 4, PAL_LIGHT_GRAY);
        vga13_fill_rect(x + 7, y + 12, 3, 3, PAL_LIGHT_GRAY);
        vga13_fill_rect(x + 11, y + 12, 3, 3, PAL_LIGHT_GRAY);
        vga13_fill_rect(x + 15, y + 12, 3, 3, PAL_LIGHT_GRAY);
    } else if (ic->app == APP_TERMINAL) {
        vga13_fill_rect(x + 7, y + 6, 10, 10, VGA13_BLACK);
        vga13_draw_string(x + 10, y + 8, ">", VGA13_WHITE, VGA13_BLACK, 0);
    } else if (ic->app == APP_DISK) {
        vga13_fill_rect(x + 6, y + 7, 12, 10, PAL_LIGHT_GRAY);
        hline_px(x + 6, x + 17, y + 7, VGA13_BLACK);
        hline_px(x + 6, x + 17, y + 16, VGA13_BLACK);
        vline_px(x + 6, y + 7, y + 16, VGA13_BLACK);
        vline_px(x + 17, y + 7, y + 16, VGA13_BLACK);
        vga13_fill_rect(x + 8, y + 5, 8, 3, VGA13_WHITE);
        hline_px(x + 8, x + 15, y + 5, VGA13_BLACK);
    } else if (ic->app == APP_TRASH) {
        draw_trash_icon(x + 4, y + 4, 0);
    } else {
        vga13_fill_rect(x + 6, y + 6, 12, 12, PAL_LIGHT_GRAY);
    }
}


static int is_icon_selected(int idx) {
    int i;
    for (i = 0; i < num_selected_icons; i++) {
        if (selected_icons[i] == idx) return 1;
    }
    return 0;
}

static void add_to_selection(int idx) {
    if (idx >= 0 && idx < desktop_icon_count && num_selected_icons < MAX_DESKTOP_ICONS && !is_icon_selected(idx)) {
        selected_icons[num_selected_icons++] = idx;
    }
}

static void remove_from_selection(int idx) {
    int i, j;
    for (i = 0; i < num_selected_icons; i++) {
        if (selected_icons[i] == idx) {
            for (j = i; j < num_selected_icons - 1; j++) {
                selected_icons[j] = selected_icons[j + 1];
            }
            num_selected_icons--;
            return;
        }
    }
}

static void clear_selection(void) {
    num_selected_icons = 0;
    selected_icon = -1;
}

static void toggle_selection(int idx) {
    if (is_icon_selected(idx)) {
        remove_from_selection(idx);
    } else {
        add_to_selection(idx);
    }
}

static void desktop_select_all_icons(void) {
    desktop_ensure_icons_built();
    num_selected_icons = 0;
    selected_icon = -1;
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

    
    for (int i = 0; i < desktop_icon_count; i++) {
        for (int j = i + 1; j < desktop_icon_count; j++) {
            if (str_cmp_simple(desktop_icons[idx[j]].label, desktop_icons[idx[i]].label) < 0) {
                int t = idx[i];
                idx[i] = idx[j];
                idx[j] = t;
            }
        }
    }

    int gx = 50;
    int gy = 22;
    int col_w = 36;
    int row_h = 40;
    int cols = (VGA13_WIDTH - 80) / col_w;
    if (cols < 1) cols = 1;

    int k = 0;
    for (int ii = 0; ii < desktop_icon_count; ii++) {
        int i = idx[ii];
        int x = gx + (k % cols) * col_w;
        int y = gy + (k / cols) * row_h;
        if (y > VGA13_HEIGHT - 60) break;
        desktop_icons[i].x = x;
        desktop_icons[i].y = y;
        k++;
    }

    desktop_needs_full_blit = 1;
}

static void desktop_clip_from_selected(int move) {
    desktop_ensure_icons_built();
    desktop_clip_valid = 0;
    desktop_clip_is_dir = 0;
    if (selected_icon < 0 || selected_icon >= desktop_icon_count) return;

    icon_t* ic = &desktop_icons[selected_icon];
    if (ic->app != APP_NONE) return; 
    if (!ic->label[0]) return;

    desktop_clip_valid = 1;
    desktop_clip_move = move;
    desktop_clip_is_dir = ic->is_dir;
    desktop_clip_src_dir = ic->parent_dir_cluster;
    str_cpy(desktop_clip_name, ic->label, FS_MAX_NAME);
}

static void desktop_clip_paste_to_desktop(void) {
    if (!desktop_clip_valid) return;

    u32 dst_cluster = desktop_folder_cluster;
    if (dst_cluster < 2) dst_cluster = fs_root_dir_cluster();
    if (dst_cluster < 2) return;

    if (desktop_clip_is_dir) {
        
        return;
    }

    if (fs_using_fat32()) {
        (void)fs_copy_file(desktop_clip_src_dir, desktop_clip_name, dst_cluster, desktop_clip_name);
        
        if (desktop_clip_move && desktop_clip_src_dir != dst_cluster) {
            (void)fs_unlink(desktop_clip_src_dir, desktop_clip_name);
        }
    } else {
        
        if (desktop_clip_move) return;
        int old_fd = fs_open(desktop_clip_name);
        if (old_fd < 0) return;
        int new_fd = fs_create(desktop_clip_name);
        if (new_fd < 0) return;
        (void)fs_write(new_fd, fs_get_data(old_fd), fs_size(old_fd));
    }

    desktop_mark_icons_dirty();
    desktop_needs_full_blit = 1;
}

static void desktop_duplicate_selected_file(void) {
    desktop_ensure_icons_built();
    if (selected_icon < 0 || selected_icon >= desktop_icon_count) return;
    icon_t* ic = &desktop_icons[selected_icon];
    if (ic->app != APP_NONE || ic->is_dir || !ic->label[0]) return;

    u32 dst_cluster = desktop_folder_cluster;
    if (dst_cluster < 2) dst_cluster = fs_root_dir_cluster();
    if (dst_cluster < 2) return;

    
    char base[9] = {0};
    char ext[4] = {0};
    int dot = -1;
    for (int i = 0; ic->label[i]; i++) if (ic->label[i] == '.') { dot = i; break; }
    if (dot >= 0) {
        int b = dot;
        if (b > 8) b = 8;
        for (int i = 0; i < b; i++) base[i] = ic->label[i];
        int e = 0;
        for (int i = dot + 1; ic->label[i] && e < 3; i++) ext[e++] = ic->label[i];
        ext[e] = 0;
    } else {
        int b = 0;
        while (ic->label[b] && b < 8) { base[b] = ic->label[b]; b++; }
        base[b] = 0;
    }

    
    char new_name[FS_MAX_NAME];
    for (int n = 1; n <= 99; n++) {
        char cand_base[9] = {0};
        
        int keep = 5;
        int base_len = str_len(base);
        if (base_len < keep) keep = base_len;
        for (int i = 0; i < keep; i++) cand_base[i] = base[i];
        int p = keep;
        if (p < 8) cand_base[p++] = 'D';
        if (p < 8) cand_base[p++] = '0' + (n / 10);
        if (p < 8) cand_base[p++] = '0' + (n % 10);
        cand_base[p] = 0;

        if (ext[0]) ksnprintf(new_name, sizeof(new_name), "%s.%s", cand_base, ext);
        else ksnprintf(new_name, sizeof(new_name), "%s", cand_base);

        if (fs_using_fat32()) {
            FSDirEnt ents[FS_MAX_FILES];
            int count = fs_list_dir(dst_cluster, ents, FS_MAX_FILES);
            int exists = 0;
            for (int i = 0; i < count; i++) if (str_eq(ents[i].name, new_name)) { exists = 1; break; }
            if (exists) continue;
            (void)fs_copy_file(ic->parent_dir_cluster, ic->label, dst_cluster, new_name);
        } else {
            if (fs_exists(new_name)) continue;
            int old_fd = fs_open(ic->label);
            if (old_fd < 0) return;
            int new_fd = fs_create(new_name);
            if (new_fd < 0) return;
            (void)fs_write(new_fd, fs_get_data(old_fd), fs_size(old_fd));
        }

        desktop_mark_icons_dirty();
        desktop_needs_full_blit = 1;
        return;
    }
}

void draw_desktop_icons(void) {
    int i;
    desktop_ensure_icons_built();
    for (i = 0; i < desktop_icon_count; i++) {
        int ix = desktop_icons[i].x;
        int iy = desktop_icons[i].y;
        int ll = str_len(desktop_icons[i].label);
        int lx = ix + (ICON_SIZE - ll * 6) / 2;
        int is_sel = is_icon_selected(i) || selected_icon == i;
        draw_icon_bitmap(ix, iy, &desktop_icons[i]);
        if (is_sel) {
            int yy, xx;
            for (yy = iy; yy < iy + ICON_SIZE; yy++) {
                for (xx = ix; xx < ix + ICON_SIZE; xx++) {
                    if (((xx + yy) & 1) == 0) vga13_put_pixel(xx, yy, PAL_DARK_GRAY);
                }
            }
            vga13_fill_rect(lx - 1, iy + ICON_SIZE + 1, ll * 6 + 2, ICON_LABEL_H, VGA13_BLACK);
        } else {
            vga13_fill_rect(lx - 1, iy + ICON_SIZE + 1, ll * 6 + 2, ICON_LABEL_H, VGA13_WHITE);
        }
        vga13_draw_string(lx, iy + ICON_SIZE + 2, desktop_icons[i].label, VGA13_BLACK, VGA13_WHITE, is_sel);
    }
}

static void layout_menus(void) {
    int i, x = 4;
    menus = current_menus;
    for (i = 0; i < MENU_COUNT; i++) {
        menus[i].x = x;
        menus[i].w = str_len(menus[i].title) * 6 + 10;
        x += menus[i].w + 2;
    }
}

static void switch_menus_for_app(app_kind_t app) {
    if (current_menu_app == app) return;
    current_menu_app = app;
    menu_open = -1;
    menu_hover = -1;
    menu_hot = -1;
    switch (app) {
    case APP_NOTEPAD:
        current_menus = np_menus;
        current_menu_count = 5;
        break;
    case APP_CALC:
        current_menus = calc_menus;
        current_menu_count = 3;
        break;
    case APP_TERMINAL:
        current_menus = term_menus;
        current_menu_count = 4;
        break;
    case APP_DISK:
        current_menus = disk_menus;
        current_menu_count = 5;
        break;
    case APP_CONTROL_PANEL:
    case APP_PUZZLE:
        current_menus = sav_only_menus;
        current_menu_count = 1;
        break;
    default:
        current_menus = desk_menus;
        current_menu_count = 5;
        break;
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
    for (i = 0; i < MENU_COUNT; i++) {
        if (mx >= menus[i].x && mx < menus[i].x + menus[i].w) return i;
    }
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

static int hit_icon(int mx, int my) {
    int i;
    desktop_ensure_icons_built();
    for (i = 0; i < desktop_icon_count; i++) {
        int x = desktop_icons[i].x;
        int y = desktop_icons[i].y;
        if (mx >= x && mx < x + ICON_SIZE && my >= y && my < y + ICON_SIZE + ICON_LABEL_H + 6) return i;
    }
    return -1;
}

static void draw_menu_bar_interactive(void) {
    int i;
    draw_global_menu_bar(0);
    for (i = 0; i < MENU_COUNT; i++) {
        int inv = ((menu_open == i) || (menu_open < 0 && menu_hot == i));
        if (inv) vga13_fill_rect(menus[i].x, 1, menus[i].w, SVS_MENU_BAR_H - 2, VGA13_BLACK);
        if (i == 0) {
            u8 c = inv ? VGA13_WHITE : VGA13_BLACK;
            hline_px(5, 11, 3, c);
            hline_px(5, 11, 9, c);
            vline_px(5, 3, 9, c);
            vline_px(11, 3, 9, c);
        } else {
            vga13_draw_string(menus[i].x + 5, 3, menus[i].title, VGA13_BLACK, VGA13_WHITE, inv);
        }
    }

    
    {
        char clk[16];
        rtc_get_time_string(clk, sizeof(clk), ui_clock_show_seconds);
        int clk_w = str_len(clk) * 6;
        int clk_x = VGA13_WIDTH - clk_w - 6;
        if (clk_x < 140) clk_x = 140; 
        vga13_draw_string(clk_x, 3, clk, VGA13_BLACK, VGA13_WHITE, 0);
    }
}

static void draw_dropdown_topmost(void) {
    int i, x, y, w, h;
    if (menu_open < 0) return;
    x = menus[menu_open].x;
    y = SVS_MENU_BAR_H;
    w = dropdown_w(menu_open);
    h = menus[menu_open].item_count * 11 + 6;
    vga13_fill_rect(x + 1, y + h, w, 1, PAL_DARK_GRAY);
    vga13_fill_rect(x + w, y + 1, 1, h, PAL_DARK_GRAY);
    vga13_fill_rect(x, y, w, h, VGA13_WHITE);
    hline_px(x, x + w - 1, y, VGA13_BLACK);
    hline_px(x, x + w - 1, y + h - 1, VGA13_BLACK);
    vline_px(x, y, y + h - 1, VGA13_BLACK);
    vline_px(x + w - 1, y, y + h - 1, VGA13_BLACK);
    for (i = 0; i < menus[menu_open].item_count; i++) {
        int iy = y + 2 + i * 11;
        int inv = (i == menu_hover);
        if (inv) vga13_fill_rect(x + 2, iy - 1, w - 4, 10, VGA13_BLACK);
        vga13_draw_string(x + 8, iy, menus[menu_open].items[i].label, VGA13_BLACK, VGA13_WHITE, inv);
    }
}

static void draw_lasso(void) {
    int x0, y0, x1, y1, i;
    if (!lasso_active) return;
    x0 = lasso_start_x < lasso_cur_x ? lasso_start_x : lasso_cur_x;
    y0 = lasso_start_y < lasso_cur_y ? lasso_start_y : lasso_cur_y;
    x1 = lasso_start_x > lasso_cur_x ? lasso_start_x : lasso_cur_x;
    y1 = lasso_start_y > lasso_cur_y ? lasso_start_y : lasso_cur_y;
    for (i = x0; i <= x1; i += 2) {
        vga13_put_pixel(i, y0, VGA13_BLACK);
        vga13_put_pixel(i, y1, VGA13_BLACK);
    }
    for (i = y0; i <= y1; i += 2) {
        vga13_put_pixel(x0, i, VGA13_BLACK);
        vga13_put_pixel(x1, i, VGA13_BLACK);
    }
}


static int ctx_menu_width(void) {
    int i, w = 70;
    for (i = 0; i < CTX_MENU_ITEMS; i++) {
        int tw = str_len(ctx_menu_labels[i]) * 6 + 16;
        if (tw > w) w = tw;
    }
    return w;
}

static void draw_context_menu(void) {
    int i, w, h;
    if (!ctx_menu_open) return;
    w = ctx_menu_width();
    h = CTX_MENU_ITEMS * 11 + 6;
    
    if (ctx_menu_x + w > VGA13_WIDTH) ctx_menu_x = VGA13_WIDTH - w;
    if (ctx_menu_y + h > VGA13_HEIGHT) ctx_menu_y = VGA13_HEIGHT - h;
    
    vga13_fill_rect(ctx_menu_x + 1, ctx_menu_y + h, w, 1, PAL_DARK_GRAY);
    vga13_fill_rect(ctx_menu_x + w, ctx_menu_y + 1, 1, h, PAL_DARK_GRAY);
    vga13_fill_rect(ctx_menu_x, ctx_menu_y, w, h, VGA13_WHITE);
    
    hline_px(ctx_menu_x, ctx_menu_x + w - 1, ctx_menu_y, VGA13_BLACK);
    hline_px(ctx_menu_x, ctx_menu_x + w - 1, ctx_menu_y + h - 1, VGA13_BLACK);
    vline_px(ctx_menu_x, ctx_menu_y, ctx_menu_y + h - 1, VGA13_BLACK);
    vline_px(ctx_menu_x + w - 1, ctx_menu_y, ctx_menu_y + h - 1, VGA13_BLACK);
    
    for (i = 0; i < CTX_MENU_ITEMS; i++) {
        int iy = ctx_menu_y + 2 + i * 11;
        int inv = (i == ctx_menu_hover);
        if (inv) vga13_fill_rect(ctx_menu_x + 2, iy - 1, w - 4, 10, VGA13_BLACK);
        vga13_draw_string(ctx_menu_x + 8, iy, ctx_menu_labels[i], VGA13_BLACK, VGA13_WHITE, inv);
    }
}

static int hit_context_menu(int mx, int my) {
    int w, h;
    if (!ctx_menu_open) return -1;
    w = ctx_menu_width();
    h = CTX_MENU_ITEMS * 11 + 6;
    if (mx < ctx_menu_x || mx >= ctx_menu_x + w ||
        my < ctx_menu_y || my >= ctx_menu_y + h) return -1;
    return (my - ctx_menu_y - 3) / 11;
}

static void context_menu_click(int mx, int my) {
    int item = hit_context_menu(mx, my);
    if (item >= 0 && item < CTX_MENU_ITEMS) {
        if (item == 0) { 
            
            fs_try_mount_fat32();
            
            u32 target_cluster = desktop_folder_cluster;
            if (target_cluster < 2 && fs_using_fat32()) {
                target_cluster = fs_root_dir_cluster();
            }
            if (target_cluster >= 2) {
                
                char nm[FS_MAX_NAME];
                int n = 1;
                nm[0] = 0;
                
                for (;;) {
                    char tmp[FS_MAX_NAME];
                    tmp[0] = 'N'; tmp[1] = 'E'; tmp[2] = 'W'; tmp[3] = 'F'; tmp[4] = 'O'; tmp[5] = 'L'; tmp[6] = 'D';
                    tmp[7] = (char)('0' + (n % 10));
                    tmp[8] = 0;
                    
                    FSDirEnt ents[FS_MAX_FILES];
                    int count = fs_list_dir(target_cluster, ents, FS_MAX_FILES);
                    int exists = 0;
                    for (int i = 0; i < count; i++) {
                        if (str_eq(ents[i].name, tmp)) { exists = 1; break; }
                    }
                    if (!exists) { str_cpy(nm, tmp, FS_MAX_NAME); break; }
                    n++;
                    if (n > 9) { str_cpy(nm, "NEWFOLD9", FS_MAX_NAME); break; }
                }
                int ret = fs_mkdir(target_cluster, nm);
                (void)ret;
                desktop_mark_icons_dirty();
            }
        } else if (item == 1) { 
            if (ctx_menu_target_icon >= 0) {
                icon_t* ic = &desktop_icons[ctx_menu_target_icon];
                if (ic->app != APP_NONE) {
                    app_open(ic->app);
                } else {
                    if (ic->is_dir) {
                        open_disk_at_cluster(ic->first_cluster, ic->label);
                    } else {
                        open_file_in_notepad(ic->parent_dir_cluster, ic->label);
                    }
                }
            }
        } else if (item == 2) { 
            win_open(APP_ABOUT, "Get Info", 60, 40, 180, 109);
        } else if (item == 3) { 
            if (ctx_menu_target_icon >= 0) {
                icon_t* ic = &desktop_icons[ctx_menu_target_icon];
                if (ic->app == APP_NONE && !ic->is_dir && ic->label[0]) {
                    u32 src_dir = ic->parent_dir_cluster;
                    if (src_dir < 2) src_dir = fs_root_dir_cluster();

                    
                    char base[9] = {0};
                    char ext[4] = {0};
                    int dot = -1;
                    for (int i = 0; ic->label[i]; i++) {
                        if (ic->label[i] == '.') { dot = i; break; }
                    }
                    if (dot >= 0) {
                        int b = dot;
                        if (b > 8) b = 8;
                        for (int i = 0; i < b; i++) base[i] = ic->label[i];
                        int e = 0;
                        for (int i = dot + 1; ic->label[i] && e < 3; i++) ext[e++] = ic->label[i];
                        ext[e] = 0;
                    } else {
                        int b = 0;
                        while (ic->label[b] && b < 8) { base[b] = ic->label[b]; b++; }
                        base[b] = 0;
                    }

                    
                    char new_name[FS_MAX_NAME];
                    new_name[0] = 0;

                    for (int n = 1; n <= 99; n++) {
                        char cand_base[9] = {0};
                        int suffix = n % 100;
                        
                        int keep = 8 - 2;
                        if (keep < 1) keep = 1;
                        int base_len = str_len(base);
                        if (base_len < keep) {
                            for (int i = 0; i < base_len; i++) cand_base[i] = base[i];
                            cand_base[base_len] = 0;
                        } else {
                            for (int i = 0; i < keep; i++) cand_base[i] = base[i];
                            cand_base[keep] = 0;
                        }
                        
                        {
                            int pos = str_len(cand_base);
                            if (pos < 8) cand_base[pos++] = 'R';
                            if (pos < 8) cand_base[pos++] = '0' + (suffix / 10);
                            if (pos < 8) cand_base[pos++] = '0' + (suffix % 10);
                            cand_base[pos] = 0;
                        }

                        if (ext[0]) {
                            ksnprintf(new_name, sizeof(new_name), "%s.%s", cand_base, ext);
                        } else {
                            ksnprintf(new_name, sizeof(new_name), "%s", cand_base);
                        }

                        
                        FSDirEnt ents[FS_MAX_FILES];
                        int count = fs_list_dir(src_dir, ents, FS_MAX_FILES);
                        int exists = 0;
                        for (int i = 0; i < count; i++) {
                            if (str_eq(ents[i].name, new_name)) { exists = 1; break; }
                        }
                        if (exists) continue;

                        
                        if (fs_using_fat32()) {
                            (void)fs_copy_file(src_dir, ic->label, src_dir, new_name);
                            (void)fs_unlink(src_dir, ic->label);
                        } else {
                            int old_fd = fs_open(ic->label);
                            if (old_fd >= 0) {
                                const char *data = fs_get_data(old_fd);
                                u32 len = fs_size(old_fd);
                                int new_fd = fs_create(new_name);
                                if (new_fd >= 0) (void)fs_write(new_fd, data, len);
                                (void)fs_unlink(src_dir, ic->label);
                            }
                        }

                        desktop_mark_icons_dirty();
                        break;
                    }
                }
            }
        } else if (item == 4) { 
            if (ctx_menu_target_icon >= 0) {
                icon_t* ic = &desktop_icons[ctx_menu_target_icon];
                if (ic->app == APP_NONE) {
                    if (ic->is_dir) (void)fs_rmdir_empty(ic->parent_dir_cluster, ic->label);
                    else (void)fs_unlink(ic->parent_dir_cluster, ic->label);
                    desktop_mark_icons_dirty();
                }
            }
        } else if (item == 5) { 
            if (ctx_menu_target_icon >= 0) {
                icon_t* ic = &desktop_icons[ctx_menu_target_icon];
                if (ic->app == APP_DISK) {
                    
                    for (int i = 0; i < MAX_WIN; i++) {
                        if (wins[i].used && wins[i].app == APP_DISK) win_close(i);
                    }
                    desktop_needs_full_blit = 1;
                }
            }
        }
    }
    ctx_menu_open = 0;
    ctx_menu_hover = -1;
    desktop_needs_full_blit = 1;
}



static void calc_copy_last_number_to_clipboard(int id) {
    if (id < 0 || id >= MAX_WIN || wins[id].app != APP_CALC) return;
    calc_clipboard_len = 0;
    calc_clipboard[0] = 0;

    
    const char *disp = calc_display[id];
    const char *eq = 0;
    for (int i = 0; disp[i]; i++) {
        if (disp[i] == '=') { eq = &disp[i]; break; }
    }

    const char *src = eq ? (eq + 1) : disp;
    while (*src == ' ') src++;

    int len = str_len(src);
    if (len >= CALC_DISPLAY_LEN) len = CALC_DISPLAY_LEN - 1;
    for (int i = 0; i < len; i++) calc_clipboard[i] = src[i];
    calc_clipboard[len] = 0;
    calc_clipboard_len = len;
}

static double calc_parse_double(const char *s) {
    
    if (!s) return 0.0;
    int i = 0;
    while (s[i] == ' ') i++;

    int neg = 0;
    if (s[i] == '-') { neg = 1; i++; }
    else if (s[i] == '+') { i++; }

    double val = 0.0;
    while (s[i] >= '0' && s[i] <= '9') {
        val = val * 10.0 + (double)(s[i] - '0');
        i++;
    }

    if (s[i] == '.') {
        i++;
        double base = 0.1;
        while (s[i] >= '0' && s[i] <= '9') {
            val += (double)(s[i] - '0') * base;
            base *= 0.1;
            i++;
        }
    }

    if (s[i] == 'e' || s[i] == 'E') {
        i++;
        int exp_neg = 0;
        if (s[i] == '-') { exp_neg = 1; i++; }
        else if (s[i] == '+') { i++; }

        int exp = 0;
        while (s[i] >= '0' && s[i] <= '9') {
            exp = exp * 10 + (s[i] - '0');
            i++;
        }

        double scale = 1.0;
        int step = exp_neg ? -exp : exp;
        if (step > 0) {
            for (int k = 0; k < step; k++) scale *= 10.0;
        } else if (step < 0) {
            for (int k = 0; k < -step; k++) scale /= 10.0;
        }
        val *= scale;
    }

    return neg ? -val : val;
}

static void calc_paste_clipboard_to_calc(int id) {
    if (id < 0 || id >= MAX_WIN || wins[id].app != APP_CALC) return;
    if (calc_clipboard_len <= 0) return;

    calc_accumulator[id] = 0;
    calc_current[id] = calc_parse_double(calc_clipboard);
    calc_operation[id] = 0;
    calc_error[id] = 0;

    
    calc_has_decimal[id] = 0;
    calc_decimal_places[id] = 0;

    const char *p = calc_clipboard;
    while (*p == ' ') p++;

    const char *dot = 0;
    for (int k = 0; p[k]; k++) {
        if (p[k] == '.') { dot = &p[k]; break; }
        if (p[k] == 'e' || p[k] == 'E') break;
    }
    if (dot) {
        calc_has_decimal[id] = 1;
        int cnt = 0;
        for (int k = 1; dot[k] && cnt < 6; k++) {
            if (dot[k] >= '0' && dot[k] <= '9') cnt++;
            else break;
        }
        calc_decimal_places[id] = cnt;
    }

    calc_new_entry[id] = 1;
    calc_left_decimal_places[id] = 0;
    calc_left_operand[id] = 0;
    calc_show_expression[id] = 0;
    calc_expr_complete[id] = 0;

    calc_update_display(id);
    desktop_needs_full_blit = 1;
}

static void disk_cut_selected(int id) {
    if (id < 0 || id >= MAX_WIN || wins[id].app != APP_DISK) return;
    int idx = disk_sel[id];
    if (idx < 0 || idx >= disk_count[id]) return;
    if (disk_entries[id][idx].is_dir) return; 

    disk_copy_set(id);
    (void)fs_unlink(disk_cwd_cluster[id], disk_entries[id][idx].name);
    disk_refresh(id);
    desktop_mark_icons_dirty();
    desktop_needs_full_blit = 1;
}



static int calc_ctx_menu_width(void) {
    int i, w = 70;
    for (i = 0; i < CALC_CTX_MENU_ITEMS; i++) {
        int tw = str_len(calc_ctx_menu_labels[i]) * 6 + 16;
        if (tw > w) w = tw;
    }
    return w;
}

static void draw_calc_context_menu(void) {
    int i, w, h;
    if (!calc_ctx_menu_open) return;
    w = calc_ctx_menu_width();
    h = CALC_CTX_MENU_ITEMS * 11 + 6;

    if (calc_ctx_menu_x + w > VGA13_WIDTH) calc_ctx_menu_x = VGA13_WIDTH - w;
    if (calc_ctx_menu_y + h > VGA13_HEIGHT) calc_ctx_menu_y = VGA13_HEIGHT - h;

    vga13_fill_rect(calc_ctx_menu_x + 1, calc_ctx_menu_y + h, w, 1, PAL_DARK_GRAY);
    vga13_fill_rect(calc_ctx_menu_x + w, calc_ctx_menu_y + 1, 1, h, PAL_DARK_GRAY);
    vga13_fill_rect(calc_ctx_menu_x, calc_ctx_menu_y, w, h, VGA13_WHITE);

    hline_px(calc_ctx_menu_x, calc_ctx_menu_x + w - 1, calc_ctx_menu_y, VGA13_BLACK);
    hline_px(calc_ctx_menu_x, calc_ctx_menu_x + w - 1, calc_ctx_menu_y + h - 1, VGA13_BLACK);
    vline_px(calc_ctx_menu_x, calc_ctx_menu_y, calc_ctx_menu_y + h - 1, VGA13_BLACK);
    vline_px(calc_ctx_menu_x + w - 1, calc_ctx_menu_y, calc_ctx_menu_y + h - 1, VGA13_BLACK);

    for (i = 0; i < CALC_CTX_MENU_ITEMS; i++) {
        int iy = calc_ctx_menu_y + 2 + i * 11;
        int inv = (i == calc_ctx_menu_hover);
        if (inv) vga13_fill_rect(calc_ctx_menu_x + 2, iy - 1, w - 4, 10, VGA13_BLACK);
        vga13_draw_string(calc_ctx_menu_x + 8, iy, calc_ctx_menu_labels[i], VGA13_BLACK, VGA13_WHITE, inv);
    }
}

static int hit_calc_context_menu(int mx, int my) {
    int w, h;
    if (!calc_ctx_menu_open) return -1;
    w = calc_ctx_menu_width();
    h = CALC_CTX_MENU_ITEMS * 11 + 6;
    if (mx < calc_ctx_menu_x || mx >= calc_ctx_menu_x + w || my < calc_ctx_menu_y || my >= calc_ctx_menu_y + h) return -1;
    return (my - calc_ctx_menu_y - 3) / 11;
}

static void calc_context_menu_click(int mx, int my) {
    int item = hit_calc_context_menu(mx, my);
    int active = win_top_id();
    if (active < 0 || wins[active].app != APP_CALC) {
        calc_ctx_menu_open = 0;
        calc_ctx_menu_hover = -1;
        desktop_needs_full_blit = 1;
        return;
    }

    if (item == 0) calc_copy_last_number_to_clipboard(active);
    else if (item == 1) calc_paste_clipboard_to_calc(active);
    else if (item == 2) { calc_scientific[active] = 0; calc_update_display(active); }
    else if (item == 3) { calc_scientific[active] = 1; calc_update_display(active); }

    calc_ctx_menu_open = 0;
    calc_ctx_menu_hover = -1;
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

static void draw_disk_context_menu(void) {
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

static int hit_disk_context_menu(int mx, int my) {
    int w, h;
    if (!disk_ctx_menu_open) return -1;
    w = disk_ctx_menu_width();
    h = DISK_CTX_MENU_ITEMS * 11 + 6;
    if (mx < disk_ctx_menu_x || mx >= disk_ctx_menu_x + w || my < disk_ctx_menu_y || my >= disk_ctx_menu_y + h) return -1;
    return (my - disk_ctx_menu_y - 3) / 11;
}

static void disk_context_menu_click(int mx, int my) {
    int item = hit_disk_context_menu(mx, my);
    int active = win_top_id();
    if (active < 0 || wins[active].app != APP_DISK) {
        disk_ctx_menu_open = 0;
        disk_ctx_menu_hover = -1;
        disk_ctx_target_idx = -1;
        desktop_needs_full_blit = 1;
        return;
    }

    if (disk_ctx_target_idx >= 0) disk_sel[active] = disk_ctx_target_idx;

    if (item == 0) {
        if (disk_sel[active] >= 0 && disk_sel[active] < disk_count[active]) handle_disk_key(active, KEY_ENTER);
    } else if (item == 1) {
        char nm[FS_MAX_NAME];
        disk_refresh(active);
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
        desktop_needs_full_blit = 1;
    } else if (item == 5) {
        handle_disk_key(active, KEY_DELETE);
        desktop_mark_icons_dirty();
        desktop_needs_full_blit = 1;
    } else if (item == 6) {
        win_close(active);
    }

    disk_ctx_menu_open = 0;
    disk_ctx_menu_hover = -1;
    disk_ctx_target_idx = -1;
    desktop_needs_full_blit = 1;
}

static void process_icon_drag(int mx, int my) {
    int dx, dy, i;
    
    if (hit_top_window(mx, my) >= 0) {
        icon_dragging = 0;
        icon_drag_idx = -1;
        return;
    }
    if (!icon_dragging || icon_drag_idx < 0) return;
    if (!(mouse_buttons & 1)) {
        icon_dragging = 0;
        icon_drag_idx = -1;
        return;
    }
    dx = mx - icon_drag_off_x - desktop_icons[icon_drag_idx].x;
    dy = my - icon_drag_off_y - desktop_icons[icon_drag_idx].y;
    if (num_selected_icons > 1 && is_icon_selected(icon_drag_idx)) {
        for (i = 0; i < num_selected_icons; i++) {
            int idx = selected_icons[i];
            desktop_icons[idx].x += dx;
            desktop_icons[idx].y += dy;
            if (desktop_icons[idx].x < 0) desktop_icons[idx].x = 0;
            if (desktop_icons[idx].y < DESKTOP_Y) desktop_icons[idx].y = DESKTOP_Y;
            if (desktop_icons[idx].x + ICON_SIZE > VGA13_WIDTH) desktop_icons[idx].x = VGA13_WIDTH - ICON_SIZE;
            if (desktop_icons[idx].y + ICON_SIZE > VGA13_HEIGHT) desktop_icons[idx].y = VGA13_HEIGHT - ICON_SIZE;
        }
    } else {
        desktop_icons[icon_drag_idx].x = mx - icon_drag_off_x;
        desktop_icons[icon_drag_idx].y = my - icon_drag_off_y;
        if (desktop_icons[icon_drag_idx].x < 0) desktop_icons[icon_drag_idx].x = 0;
        if (desktop_icons[icon_drag_idx].y < DESKTOP_Y) desktop_icons[icon_drag_idx].y = DESKTOP_Y;
        if (desktop_icons[icon_drag_idx].x + ICON_SIZE > VGA13_WIDTH) desktop_icons[icon_drag_idx].x = VGA13_WIDTH - ICON_SIZE;
        if (desktop_icons[icon_drag_idx].y + ICON_SIZE > VGA13_HEIGHT) desktop_icons[icon_drag_idx].y = VGA13_HEIGHT - ICON_SIZE;
    }
    if (dx != 0 || dy != 0) desktop_needs_full_blit = 1;
    icon_drag_off_x = mx - desktop_icons[icon_drag_idx].x;
    icon_drag_off_y = my - desktop_icons[icon_drag_idx].y;
}

static void desktop_manager_click(int mx, int my, int left_edge) {
    int wid, ic, mtitle, mitem;
    u32 t;
    if (!left_edge) return;

    mtitle = hit_menu_title(mx, my);
    if (mtitle >= 0) {
        menu_open = mtitle;
        menu_hover = -1;
        clear_selection();
        return;
    }

    if (menu_open >= 0) {
        mitem = hit_menu_item(mx, my);
        if (mitem >= 0) {
            app_kind_t app = menus[menu_open].items[mitem].app;
            const char *label = menus[menu_open].items[mitem].label;
            
            
            if (app == APP_NONE && current_menu_app == APP_NOTEPAD) {
                int active = win_top_id();
                if (active >= 0 && wins[active].app == APP_NOTEPAD) {
                    if (str_eq(label, "Undo")) {
                        notepad_undo(active);
                        desktop_needs_full_blit = 1;
                    } else if (str_eq(label, "Redo")) {
                        notepad_redo(active);
                        desktop_needs_full_blit = 1;
                    } else if (str_eq(label, "Cut")) {
                        notepad_cut(active);
                        desktop_needs_full_blit = 1;
                    } else if (str_eq(label, "Copy")) {
                        notepad_copy(active);
                        desktop_needs_full_blit = 1;
                    } else if (str_eq(label, "Paste")) {
                        notepad_paste(active);
                        desktop_needs_full_blit = 1;
                    } else if (str_eq(label, "Clear")) {
                        notepad_clear(active);
                        desktop_needs_full_blit = 1;
                    } else if (str_eq(label, "Open...")) {
                        
                        int best_disk = -1, best_z = -1;
                        for (int i = 0; i < MAX_WIN; i++) {
                            if (wins[i].used && wins[i].app == APP_DISK && wins[i].z > best_z) {
                                best_z = wins[i].z;
                                best_disk = i;
                            }
                        }
                        if (best_disk >= 0) handle_disk_key(best_disk, KEY_ENTER);
                    } else if (str_eq(label, "Save")) {
                        notepad_save(active);
                        desktop_needs_full_blit = 1;
                    } else if (str_eq(label, "Save As...")) {
                        notepad_save_as(active);
                        desktop_needs_full_blit = 1;
                    } else if (str_eq(label, "Close")) {
                        win_close(active);
                        desktop_needs_full_blit = 1;
                    } else if (str_eq(label, "Find...")) {
                        notepad_find_next(active, 1);
                    } else if (str_eq(label, "Find Again")) {
                        notepad_find_next(active, 0);
                    } else if (str_eq(label, "Font...")) {
                        
                    } else if (str_eq(label, "Style")) {
                        
                    } else if (str_eq(label, "Select All")) {
                        notepad_select_all(active);
                        desktop_needs_full_blit = 1;
                        menu_open = -1;
                        menu_hover = -1;
                        return;
                    }
                }
            } else if (app == APP_NONE && current_menu_app == APP_DISK) {
                int active = win_top_id();
                if (active >= 0 && wins[active].app == APP_DISK) {
                    if (str_eq(label, "New Folder")) {
                        char nm[FS_MAX_NAME];
                        disk_refresh(active);
                        disk_make_new_folder_name(active, nm);
                        (void)fs_mkdir(disk_cwd_cluster[active], nm);
                        disk_refresh(active);
                        desktop_mark_icons_dirty();
                        desktop_needs_full_blit = 1;
                    } else if (str_eq(label, "Open")) {
                        handle_disk_key(active, KEY_ENTER);
                        desktop_needs_full_blit = 1;
                    } else if (str_eq(label, "Close Window")) {
                        win_close(active);
                        desktop_needs_full_blit = 1;
                    } else if (str_eq(label, "Copy")) {
                        disk_copy_set(active);
                        desktop_needs_full_blit = 1;
                    } else if (str_eq(label, "Paste")) {
                        disk_paste(active);
                        desktop_needs_full_blit = 1;
                    } else if (str_eq(label, "Delete")) {
                        handle_disk_key(active, KEY_DELETE);
                        desktop_mark_icons_dirty();
                        desktop_needs_full_blit = 1;
                    } else if (str_eq(label, "Up one level")) {
                        handle_disk_key(active, KEY_BACKSP);
                        desktop_needs_full_blit = 1;
                    } else if (str_eq(label, "Cut")) {
                        disk_cut_selected(active);
                    } else if (str_eq(label, "By Name")) {
                        disk_sort_entries_by_name(active);
                    } else if (str_eq(label, "Clean Up")) {
                        disk_sort_entries_by_name(active);
                        disk_sel[active] = 0;
                        disk_refresh(active);
                    } else if (str_eq(label, "By Icon")) {
                        
                    } else if (str_eq(label, "By Date")) {
                        
                    } else if (str_eq(label, "Eject Disk")) {
                        win_close(active);
                    } else if (str_eq(label, "Erase Disk")) {
                        
                    } else if (str_eq(label, "Set Startup")) {
                        
                    }
                }
            } else if (app == APP_NONE && current_menu_app == APP_TERMINAL) {
                int active = win_top_id();
                if (active >= 0 && wins[active].app == APP_TERMINAL) {
                    if (str_eq(label, "Close")) {
                        win_close(active);
                        desktop_needs_full_blit = 1;
                    } else if (str_eq(label, "Run...")) {
                        handle_terminal_key(active, KEY_ENTER);
                        desktop_needs_full_blit = 1;
                    } else if (str_eq(label, "Cut")) {
                        term_build_output_text(active, 1);
                    } else if (str_eq(label, "Copy")) {
                        term_build_output_text(active, 0);
                    } else if (str_eq(label, "Paste")) {
                        term_paste_clipboard_to_input(active);
                    } else if (str_eq(label, "Clear")) {
                        term_clear(active);
                        desktop_needs_full_blit = 1;
                    } else if (str_eq(label, "Standard")) {
                        
                    } else if (str_eq(label, "Scientific")) {
                        
                    }
                }
            } else if (app == APP_NONE && current_menu_app == APP_CALC) {
                int active = win_top_id();
                if (active >= 0 && wins[active].app == APP_CALC) {
                    if (str_eq(label, "Copy")) {
                        calc_copy_last_number_to_clipboard(active);
                    } else if (str_eq(label, "Paste")) {
                        calc_paste_clipboard_to_calc(active);
                    } else if (str_eq(label, "Standard")) {
                        calc_scientific[active] = 0;
                        calc_update_display(active);
                    } else if (str_eq(label, "Scientific")) {
                        calc_scientific[active] = 1;
                        calc_update_display(active);
                    }
                }
            } else if (app == APP_NONE && current_menu_app == APP_NONE) {
                
                if (str_eq(label, "New Folder")) {
                    
                    fs_try_mount_fat32();
                    
                    u32 target_cluster = desktop_folder_cluster;
                    if (target_cluster < 2 && fs_using_fat32()) {
                        target_cluster = fs_root_dir_cluster();
                    }
                    if (target_cluster >= 2) {
                        char nm[FS_MAX_NAME];
                        int n = 1;
                        nm[0] = 0;
                        
                        for (;;) {
                            char tmp[FS_MAX_NAME];
                            tmp[0] = 'N'; tmp[1] = 'E'; tmp[2] = 'W'; tmp[3] = 'F'; tmp[4] = 'O'; tmp[5] = 'L'; tmp[6] = 'D';
                            tmp[7] = (char)('0' + (n % 10));
                            tmp[8] = 0;
                            
                            FSDirEnt ents[FS_MAX_FILES];
                            int count = fs_list_dir(target_cluster, ents, FS_MAX_FILES);
                            int exists = 0;
                            for (int i = 0; i < count; i++) {
                                if (str_eq(ents[i].name, tmp)) { exists = 1; break; }
                            }
                            if (!exists) { str_cpy(nm, tmp, FS_MAX_NAME); break; }
                            n++;
                            if (n > 9) { str_cpy(nm, "NEWFOLD9", FS_MAX_NAME); break; }
                        }
                        int ret = fs_mkdir(target_cluster, nm);
                        (void)ret;
                        desktop_mark_icons_dirty();
                        desktop_needs_full_blit = 1;
                    }
                } else if (str_eq(label, "Open")) {
                    
                    if (selected_icon >= 0) {
                        icon_t* ic = &desktop_icons[selected_icon];
                        if (ic->app != APP_NONE) {
                            app_open(ic->app);
                        } else if (ic->is_dir) {
                            open_disk_at_cluster(ic->first_cluster, ic->label);
                        } else {
                            open_file_in_notepad(ic->parent_dir_cluster, ic->label);
                        }
                    }
                } else if (str_eq(label, "Close")) {
                    
                    int top = win_top_id();
                    if (top >= 0) win_close(top);
                } else if (str_eq(label, "Get Info")) {
                    win_open(APP_ABOUT, "Get Info", 60, 40, 180, 100);
                } else if (str_eq(label, "Duplicate")) {
                    desktop_duplicate_selected_file();
                } else if (str_eq(label, "Cut")) {
                    desktop_clip_from_selected(1);
                } else if (str_eq(label, "Copy")) {
                    desktop_clip_from_selected(0);
                } else if (str_eq(label, "Paste")) {
                    desktop_clip_paste_to_desktop();
                } else if (str_eq(label, "Select All")) {
                    desktop_select_all_icons();
                } else if (str_eq(label, "By Icon")) {
                    
                } else if (str_eq(label, "By Name")) {
                    desktop_relayout_by_name();
                } else if (str_eq(label, "By Date")) {
                    
                } else if (str_eq(label, "Clean Up")) {
                    desktop_relayout_by_name();
                } else if (str_eq(label, "Eject Disk")) {
                    for (int i = 0; i < MAX_WIN; i++) if (wins[i].used && wins[i].app == APP_DISK) win_close(i);
                    desktop_needs_full_blit = 1;
                } else if (str_eq(label, "Erase Disk")) {
                    
                } else if (str_eq(label, "Set Startup")) {
                    
                }
            } else if (app != APP_NONE) {
                app_open(app);
            } else if (str_eq(label, "Shut Down...")) {
                __asm__ volatile ("cli; hlt");
            }
        }
        menu_open = -1;
        menu_hover = -1;
        return;
    }

    
    wid = hit_top_window(mx, my);
    if (wid >= 0) {
        if (hit_close(wid, mx, my)) { win_close(wid); return; }
        bring_to_front(wid);
        if (hit_title_drag(wid, mx, my)) {
            drag_id = wid;
            drag_off_x = mx - wins[wid].x;
            drag_off_y = my - wins[wid].y;
        }
        return;
    }

    ic = hit_icon(mx, my);
    if (ic >= 0) {
        int dx, dy;
        t = timer_ticks();
        if (shift_held) {
            toggle_selection(ic);
            selected_icon = ic;
        } else if (is_icon_selected(ic)) {
            selected_icon = ic;
        } else {
            clear_selection();
            add_to_selection(ic);
            selected_icon = ic;
        }
        dx = mx - last_click_x;
        dy = my - last_click_y;
        if (last_click_icon == ic && (t - last_click_ticks) <= DBL_CLICK_TICKS &&
            dx >= -DBL_CLICK_DIST && dx <= DBL_CLICK_DIST &&
            dy >= -DBL_CLICK_DIST && dy <= DBL_CLICK_DIST) {
            if (desktop_icons[ic].app != APP_NONE) {
                app_open(desktop_icons[ic].app);
            } else {
                if (desktop_icons[ic].is_dir) {
                    open_disk_at_cluster(desktop_icons[ic].first_cluster, desktop_icons[ic].label);
                } else {
                    open_file_in_notepad(desktop_icons[ic].parent_dir_cluster, desktop_icons[ic].label);
                }
            }
        } else {
            icon_drag_idx = ic;
            icon_drag_off_x = mx - desktop_icons[ic].x;
            icon_drag_off_y = my - desktop_icons[ic].y;
            icon_dragging = 1;
        }
        last_click_icon = ic;
        last_click_ticks = t;
        last_click_x = mx;
        last_click_y = my;
        return;
    }

    if (!shift_held) clear_selection();
    
    lasso_active = 1;
    lasso_start_x = mx;
    lasso_start_y = my;
    lasso_cur_x = mx;
    lasso_cur_y = my;
}

static void handle_notepad_scrollbar_click(int id, int mx, int my) {
    int cx, cy, cw, ch;
    int scrollbar_w = 11;
    int content_h;
    int thumb_y;
    int visible_lines, total_lines;
    int thumb_h;
    int click_in_track;
    int scroll_x, scroll_y, scroll_h;
    
    if (id < 0 || id >= MAX_WIN || wins[id].app != APP_NOTEPAD) return;
    
    cx = wins[id].x + 3;
    cy = wins[id].y + SVS_TITLE_H + 1;
    cw = wins[id].w - 6;
    ch = wins[id].h - SVS_TITLE_H - 3;
    content_h = ch - 4;
    visible_lines = content_h / 8;
    scroll_x = cx + cw - scrollbar_w - 1;
    scroll_y = cy + 2;
    scroll_h = content_h;
    
    total_lines = notepad_total_lines[id];
    if (total_lines <= 1) {
        int p;
        total_lines = 1;
        for (p = 0; p < notepad_buf_len[id]; p++) {
            if (notepad_buf[id][p] == '\n') total_lines++;
        }
        notepad_total_lines[id] = total_lines;
    }
    
    if (total_lines <= visible_lines) {
        thumb_h = scroll_h - 22;
        thumb_y = scroll_y + 11;
    } else {
        thumb_h = (visible_lines * (scroll_h - 22)) / total_lines;
        if (thumb_h < 16) thumb_h = 16;
        thumb_y = scroll_y + 11 + (notepad_scroll_y[id] * (scroll_h - 22 - thumb_h)) /
                  (total_lines - visible_lines);
    }
    
    
    if (mx < cx || mx >= cx + cw || my < cy || my >= cy + ch) {
        return;
    }
    
    
    if (mx < scroll_x || mx >= scroll_x + scrollbar_w ||
        my < scroll_y || my >= scroll_y + scroll_h) {
        return;
    }
    
    
    if (my >= scroll_y + 2 && my < scroll_y + 12) {
        if (notepad_scroll_y[id] > 0) {
            notepad_scroll_y[id]--;
            desktop_needs_full_blit = 1;
        }
        notepad_scrollbar_dragging = -1;  
        notepad_manual_scroll_timer = 30;  
        return;
    }
    
    
    if (my >= scroll_y + scroll_h - 12 && my < scroll_y + scroll_h - 2) {
        if (notepad_scroll_y[id] < total_lines - visible_lines) {
            notepad_scroll_y[id]++;
            desktop_needs_full_blit = 1;
        }
        notepad_scrollbar_dragging = -1;  
        notepad_manual_scroll_timer = 30;  
        return;
    }
    
    
    click_in_track = (my >= scroll_y + 13 && my < scroll_y + scroll_h - 13);
    
    if (click_in_track && total_lines > visible_lines) {
        
        if (my < thumb_y) {
            
            notepad_scroll_y[id] -= visible_lines;
            if (notepad_scroll_y[id] < 0) notepad_scroll_y[id] = 0;
            desktop_needs_full_blit = 1;
            notepad_manual_scroll_timer = 5;
        } else if (my >= thumb_y + thumb_h) {
            
            notepad_scroll_y[id] += visible_lines;
            if (notepad_scroll_y[id] > total_lines - visible_lines)
                notepad_scroll_y[id] = total_lines - visible_lines;
            desktop_needs_full_blit = 1;
            notepad_manual_scroll_timer = 5;
        } else {
            
            notepad_scrollbar_dragging = id;
            notepad_scrollbar_drag_start_y = my;
            notepad_scrollbar_drag_start_scroll = notepad_scroll_y[id];
            desktop_needs_full_blit = 1;
        }
    }
}

static void handle_disk_key(int id, int key) {
    if (id < 0 || id >= MAX_WIN || wins[id].app != APP_DISK) return;
    
    if (key == 0x03) { 
        disk_copy_set(id);
        return;
    }
    if (key == 0x16) { 
        disk_paste(id);
        desktop_needs_full_blit = 1;
        return;
    }
    if (key == KEY_UP) {
        if (disk_sel[id] > 0) disk_sel[id]--;
        desktop_needs_full_blit = 1;
        return;
    }
    if (key == KEY_DOWN) {
        if (disk_sel[id] < disk_count[id] - 1) disk_sel[id]++;
        desktop_needs_full_blit = 1;
        return;
    }
    if (key == KEY_ENTER) {
        int idx = disk_sel[id];
        if (idx >= 0 && idx < disk_count[id]) {
            const FSDirEnt* de = &disk_entries[id][idx];
            const char* name = de->name;
            if (!name || !name[0]) return;

            if (de->is_dir) {
                
                disk_parent_cluster[id] = disk_cwd_cluster[id];
                disk_cwd_cluster[id] = de->first_cluster;
                disk_sel[id] = 0;
                disk_refresh(id);
                desktop_needs_full_blit = 1;
                return;
            }

                
                int np = -1;
                for (int i = 0; i < MAX_WIN; i++) {
                    if (wins[i].used && wins[i].app == APP_NOTEPAD) { np = i; break; }
                }
                if (np < 0) {
                    app_open(APP_NOTEPAD);
                    np = win_top_id();
                }
                if (np >= 0) {
                    bring_to_front(np);

                    
                    char tmp[NOTEPAD_BUF_SIZE];
                    int n = fs_read_file_in_dir(disk_cwd_cluster[id], name, tmp, NOTEPAD_BUF_SIZE - 1);
                    if (n < 0) n = 0;
                    tmp[n] = 0;

                    int out = 0;
                    for (int p = 0; p < n && out < NOTEPAD_BUF_SIZE - 1; p++) {
                        
                        if (tmp[p] == '\r') continue;
                        notepad_buf[np][out++] = tmp[p];
                    }
                    notepad_buf[np][out] = 0;
                    notepad_buf_len[np] = out;

                    notepad_cursor_pos[np] = out;
                    notepad_cursor_x[np] = 0;
                    notepad_cursor_y[np] = 0;
                    notepad_scroll_y[np] = 0;

                    notepad_sel_dragging = -1;
                    notepad_scrollbar_dragging = -1;
                    notepad_manual_scroll_timer = 0;
                    notepad_ctx_menu_open = 0;
                    notepad_ctx_menu_hover = -1;
                    notepad_has_selection[np] = 0;
                    notepad_sel_start[np] = 0;
                    notepad_sel_end[np] = 0;

                    notepad_undo_len[np] = 0;
                    notepad_has_undo[np] = 0;
                    notepad_has_redo[np] = 0;

                    notepad_clipboard_len = 0;
                    notepad_find_has_pattern[np] = 0;
                    notepad_find_pattern[np][0] = 0;
                    notepad_find_from_pos[np] = 0;
                    notepad_set_file_association(np, disk_cwd_cluster[id], name);
                    notepad_total_lines[np] = 1;

                    str_cpy(wins[np].title, name, sizeof(wins[np].title));
                    desktop_needs_full_blit = 1;
                }
        }
        return;
    }
    if (key == KEY_BACKSP || key == KEY_LEFT) {
        
        if (disk_parent_cluster[id] >= 2) {
            disk_cwd_cluster[id] = disk_parent_cluster[id];
            disk_parent_cluster[id] = 0;
            disk_sel[id] = 0;
            disk_refresh(id);
            desktop_needs_full_blit = 1;
        }
        return;
    }
    if (key == KEY_DELETE) {
        if (disk_count[id] > 0) {
            int idx = disk_sel[id];
            if (idx >= 0 && idx < disk_count[id]) {
                FSDirEnt* de = &disk_entries[id][idx];
                if (de->is_dir) {
                    (void)fs_rmdir_empty(disk_cwd_cluster[id], de->name);
                } else {
                    (void)fs_unlink(disk_cwd_cluster[id], de->name);
                }
            }
            disk_count[id] = fs_list_dir(disk_cwd_cluster[id], disk_entries[id], FS_MAX_FILES);
            if (disk_sel[id] >= disk_count[id]) disk_sel[id] = disk_count[id] - 1;
            if (disk_sel[id] < 0) disk_sel[id] = 0;
            desktop_mark_icons_dirty();
            desktop_needs_full_blit = 1;
        }
    }
}

static void handle_notepad_key(int id, int key) {
    int i, pos;
    if (id < 0 || id >= MAX_WIN || wins[id].app != APP_NOTEPAD) return;

    
    if (key == 0x03) { 
        notepad_copy(id);
        return;
    }
    if (key == 0x18) { 
        notepad_cut(id);
        return;
    }
    if (key == 0x16) { 
        notepad_paste(id);
        return;
    }
    if (key == 0x01) { 
        notepad_select_all(id);
        return;
    }
    if (key == 0x1A) { 
        notepad_undo(id);
        return;
    }
    if (key == 0x19) { 
        notepad_redo(id);
        return;
    }

    if (key == KEY_BACKSP) {
        pos = notepad_cursor_pos[id];
        if (pos > 0 && pos <= notepad_buf_len[id]) {
            notepad_save_undo(id);
            
            for (i = pos - 1; i < notepad_buf_len[id] - 1; i++) {
                notepad_buf[id][i] = notepad_buf[id][i + 1];
            }
            notepad_buf_len[id]--;
            notepad_buf[id][notepad_buf_len[id]] = 0;
            notepad_cursor_pos[id]--;
        }
        desktop_needs_full_blit = 1;
        return;
    }

    if (key == KEY_ENTER) {
        
        if (notepad_buf_len[id] < NOTEPAD_BUF_SIZE - 1) {
            notepad_save_undo(id);
            pos = notepad_cursor_pos[id];
            
            for (i = notepad_buf_len[id]; i > pos; i--) {
                notepad_buf[id][i] = notepad_buf[id][i - 1];
            }
            notepad_buf[id][pos] = '\n';
            notepad_buf_len[id]++;
            notepad_buf[id][notepad_buf_len[id]] = 0;
            notepad_cursor_pos[id]++;
        }
        desktop_needs_full_blit = 1;
        return;
    }

    
    if (key == KEY_LEFT) {
        if (notepad_cursor_pos[id] > 0) {
            notepad_cursor_pos[id]--;
        }
        desktop_needs_full_blit = 1;
        return;
    }

    if (key == KEY_RIGHT) {
        if (notepad_cursor_pos[id] < notepad_buf_len[id]) {
            notepad_cursor_pos[id]++;
        }
        desktop_needs_full_blit = 1;
        return;
    }

    
    if (key >= 32 && key <= 126) {
        if (notepad_buf_len[id] < NOTEPAD_BUF_SIZE - 1) {
            pos = notepad_cursor_pos[id];
            
            for (i = notepad_buf_len[id]; i > pos; i--) {
                notepad_buf[id][i] = notepad_buf[id][i - 1];
            }
            notepad_buf[id][pos] = (char)key;
            notepad_buf_len[id]++;
            notepad_buf[id][notepad_buf_len[id]] = 0;
            notepad_cursor_pos[id]++;
        }
        desktop_needs_full_blit = 1;
    }
}

static void process_drag(int mx, int my) {
    int ox, oy;
    if (drag_id < 0 || !(mouse_buttons & 1) || menu_open >= 0) return;
    ox = wins[drag_id].x;
    oy = wins[drag_id].y;
    wins[drag_id].x = mx - drag_off_x;
    wins[drag_id].y = my - drag_off_y;
    win_clamp(drag_id);
    if (wins[drag_id].x != ox || wins[drag_id].y != oy) desktop_needs_full_blit = 1;
}

static void frame_loop(void) {
    int mx = (int)mouse_x;
    int my = (int)mouse_y;
    static int prev_mx = -1, prev_my = -1;  
    u8 b = mouse_buttons;
    u8 left_edge = (u8)((b & 1) && !(prev_buttons & 1));
    u8 left_up = (u8)(!(b & 1) && (prev_buttons & 1));
    u8 right_edge = (u8)((b & 2) && !(prev_buttons & 2));
    int old_hot = menu_hot;
    int old_hover = menu_hover;
    int old_open = menu_open;
    int old_sel = selected_icon;
    int old_ctx_hover = ctx_menu_hover;
    int i, x0, y0, x1, y1;
    int any_animating = 0;
    int click_consumed = 0;

    
    {
        int active = win_top_id();
        if (active >= 0) {
            switch_menus_for_app(wins[active].app);
        } else {
            switch_menus_for_app(APP_NONE);
        }
    }

    
    if (ctx_menu_open) {
        ctx_menu_hover = hit_context_menu(mx, my);
        if (left_edge) {
            context_menu_click(mx, my);
            prev_buttons = b;
            desktop_needs_full_blit = 1;
            return;
        }
        
        if (left_up && hit_context_menu(mx, my) < 0) {
            ctx_menu_open = 0;
            ctx_menu_hover = -1;
            desktop_needs_full_blit = 1;
        }
    }

    
    if (term_ctx_menu_open) {
        term_ctx_menu_hover = hit_term_context_menu(mx, my);
        if (left_edge) {
            term_context_menu_click(mx, my);
            prev_buttons = b;
            desktop_needs_full_blit = 1;
            return;
        }
        if (left_up && hit_term_context_menu(mx, my) < 0) {
            term_ctx_menu_open = 0;
            term_ctx_menu_hover = -1;
            desktop_needs_full_blit = 1;
        }
    }

    
    if (calc_ctx_menu_open) {
        calc_ctx_menu_hover = hit_calc_context_menu(mx, my);
        if (left_edge) {
            calc_context_menu_click(mx, my);
            prev_buttons = b;
            desktop_needs_full_blit = 1;
            return;
        }
        if (left_up && hit_calc_context_menu(mx, my) < 0) {
            calc_ctx_menu_open = 0;
            calc_ctx_menu_hover = -1;
            desktop_needs_full_blit = 1;
        }
    }

    
    if (disk_ctx_menu_open) {
        disk_ctx_menu_hover = hit_disk_context_menu(mx, my);
        if (left_edge) {
            disk_context_menu_click(mx, my);
            prev_buttons = b;
            desktop_needs_full_blit = 1;
            return;
        }
        if (left_up && hit_disk_context_menu(mx, my) < 0) {
            disk_ctx_menu_open = 0;
            disk_ctx_menu_hover = -1;
            disk_ctx_target_idx = -1;
            desktop_needs_full_blit = 1;
        }
    }

    
    if (right_edge && !ctx_menu_open && !notepad_ctx_menu_open &&
        !term_ctx_menu_open && !calc_ctx_menu_open && !disk_ctx_menu_open) {

        int wid = hit_top_window(mx, my);

        
        if (wid < 0) {
            ctx_menu_x = mx;
            ctx_menu_y = my;
            ctx_menu_open = 1;
            ctx_menu_hover = -1;
            ctx_menu_target_icon = hit_icon(mx, my);
            desktop_needs_full_blit = 1;
            prev_buttons = b;
            return;
        }

        
        if (wins[wid].app == APP_TERMINAL) {
            term_ctx_menu_x = mx;
            term_ctx_menu_y = my;
            term_ctx_menu_open = 1;
            term_ctx_menu_hover = -1;
            desktop_needs_full_blit = 1;
            prev_buttons = b;
            return;
        }
        if (wins[wid].app == APP_CALC) {
            calc_ctx_menu_x = mx;
            calc_ctx_menu_y = my;
            calc_ctx_menu_open = 1;
            calc_ctx_menu_hover = -1;
            desktop_needs_full_blit = 1;
            prev_buttons = b;
            return;
        }
        if (wins[wid].app == APP_DISK) {
            int cx = wins[wid].x + 3;
            int cy = wins[wid].y + SVS_TITLE_H + 1;
            int cw = wins[wid].w - 6;
            int ch = wins[wid].h - SVS_TITLE_H - 3;

            int rows = (ch - 24) / 8;
            if (rows < 1) rows = 1;
            int list_y0 = cy + 12;
            int idx = -1;
            int list_y1 = list_y0 + rows * 8;
            if (mx >= cx && mx < cx + cw && my >= list_y0 && my < list_y1) {
                idx = (my - list_y0) / 8;
                if (idx < 0 || idx >= rows || idx >= disk_count[wid]) idx = -1;
            }

            disk_ctx_menu_x = mx;
            disk_ctx_menu_y = my;
            disk_ctx_menu_open = 1;
            disk_ctx_menu_hover = -1;
            disk_ctx_target_idx = idx;
            desktop_needs_full_blit = 1;
            prev_buttons = b;
            return;
        }

        
    }

    if (left_up) {
        drag_id = -1;
        if (lasso_active) {
            x0 = lasso_start_x < lasso_cur_x ? lasso_start_x : lasso_cur_x;
            y0 = lasso_start_y < lasso_cur_y ? lasso_start_y : lasso_cur_y;
            x1 = lasso_start_x > lasso_cur_x ? lasso_start_x : lasso_cur_x;
            y1 = lasso_start_y > lasso_cur_y ? lasso_start_y : lasso_cur_y;
            desktop_ensure_icons_built();
            for (i = 0; i < desktop_icon_count; i++) {
                int ix = desktop_icons[i].x;
                int iy = desktop_icons[i].y;
                if (ix + ICON_SIZE >= x0 && ix <= x1 && iy + ICON_SIZE >= y0 && iy <= y1) {
                    if (!is_icon_selected(i)) add_to_selection(i);
                }
            }
            lasso_active = 0;
        }
        icon_dragging = 0;
        icon_drag_idx = -1;
    }
    if (lasso_active && (b & 1)) {
        lasso_cur_x = mx;
        lasso_cur_y = my;
        desktop_needs_full_blit = 1;
    }
    process_icon_drag(mx, my);
    menu_hot = hit_menu_title(mx, my);
    menu_hover = hit_menu_item(mx, my);
    
    
    click_consumed = 0;
    if (left_edge && menu_open >= 0 && hit_menu_item(mx, my) >= 0) {
        click_consumed = 1;
    }
    
    desktop_manager_click(mx, my, left_edge);
    
    
    {
        int active = win_top_id();
        if (active >= 0 && wins[active].app == APP_DISK) {
            int cx = wins[active].x + 3;
            int cy = wins[active].y + SVS_TITLE_H + 1;
            int cw = wins[active].w - 6;
            int ch = wins[active].h - SVS_TITLE_H - 3;

            
            int rows = (ch - 24) / 8;
            if (rows < 1) rows = 1;
            int list_y0 = cy + 12;
            int list_y1 = list_y0 + rows * 8;

            if (left_edge) {
                if (mx >= cx && mx < cx + cw && my >= list_y0 && my < list_y1) {
                    int idx = (my - list_y0) / 8;
                    if (idx >= 0 && idx < rows && idx < disk_count[active]) {
                        disk_sel[active] = idx;

                        
                        u32 t = timer_ticks();
                        int dx = mx - last_click_disk_x;
                        int dy = my - last_click_disk_y;
                        if (last_click_disk_sel == idx &&
                            (t - last_click_disk_ticks) <= DBL_CLICK_TICKS &&
                            dx >= -DBL_CLICK_DIST && dx <= DBL_CLICK_DIST &&
                            dy >= -DBL_CLICK_DIST && dy <= DBL_CLICK_DIST) {
                            handle_disk_key(active, KEY_ENTER);
                            last_click_disk_sel = idx;
                            last_click_disk_ticks = t;
                            last_click_disk_x = mx;
                            last_click_disk_y = my;
                        } else {
                            last_click_disk_sel = idx;
                            last_click_disk_ticks = t;
                            last_click_disk_x = mx;
                            last_click_disk_y = my;
                        }

                        desktop_needs_full_blit = 1;
                    }
                }
            }
        }
    }
    
    
    {
        int active = win_top_id();
        if (active >= 0 && wins[active].app == APP_NOTEPAD) {
            if (left_edge && !click_consumed) {
                handle_notepad_scrollbar_click(active, mx, my);
            }
            
            if (notepad_scrollbar_dragging >= 0) {
                if (!(b & 1)) {
                    
                    notepad_scrollbar_dragging = -1;
                } else {
                    
                    int id = notepad_scrollbar_dragging;
                    int ch = wins[id].h - SVS_TITLE_H - 3;
                    int content_h = ch - 4;
                    int scroll_h = content_h;
                    int total_lines = notepad_total_lines[id];
                    int visible_lines = content_h / 8;
                    int thumb_track_h = scroll_h - 26;
                    int max_scroll = total_lines - visible_lines;
                    int delta_y = my - notepad_scrollbar_drag_start_y;
                    int pixels_per_line = (max_scroll > 0) ? thumb_track_h / max_scroll : 0;
                    if (pixels_per_line < 1) pixels_per_line = 1;
                    
                    notepad_scroll_y[id] = notepad_scrollbar_drag_start_scroll + (delta_y / pixels_per_line);
                    if (notepad_scroll_y[id] < 0) notepad_scroll_y[id] = 0;
                    if (notepad_scroll_y[id] > max_scroll) notepad_scroll_y[id] = max_scroll;
                    desktop_needs_full_blit = 1;
                    notepad_manual_scroll_timer = 30;  
                }
            }
        }
    }
    
    
    {
        int active = win_top_id();
        if (active >= 0 && wins[active].app == APP_TERMINAL) {
            int cx = wins[active].x + 3;
            int cy = wins[active].y + SVS_TITLE_H + 1;
            int cw = wins[active].w - 6;
            int ch = wins[active].h - SVS_TITLE_H - 3;
            if (left_edge && !click_consumed) {
                handle_terminal_scrollbar_click(active, mx, my, cx, cy, cw, ch);
            }
            
            if (term_scrollbar_dragging >= 0) {
                if (!(b & 1)) {
                    
                    term_scrollbar_dragging = -1;
                } else {
                    
                    int id = term_scrollbar_dragging;
                    int content_h = ch - 4;
                    int scroll_h = content_h;
                    int scrollbar_w = 11;
                    int char_width = 6;
                    int chars_per_line = (cw - scrollbar_w - 2 - 4) / char_width;
                    int visible_lines = content_h / 8;
                    int total_wrapped_lines = 0;
                    int i;
                    
                    
                    for (i = 0; i < term_get_line_count(id); i++) {
                        const char *line = term_get_line(id, i);
                        int line_len = 0;
                        while (line && line[line_len]) line_len++;
                        int wrapped = (line_len + chars_per_line - 1) / chars_per_line;
                        if (wrapped < 1) wrapped = 1;
                        total_wrapped_lines += wrapped;
                    }
                    if (total_wrapped_lines < 1) total_wrapped_lines = 1;
                    
                    if (total_wrapped_lines > visible_lines) {
                        int thumb_track_h = scroll_h - 22;
                        int max_scroll = total_wrapped_lines - visible_lines;
                        int delta_y = my - term_scrollbar_drag_start_y;
                        int pixels_per_line = (thumb_track_h > 0 && max_scroll > 0) ? thumb_track_h / max_scroll : 1;
                        if (pixels_per_line < 1) pixels_per_line = 1;
                        
                        int new_scroll = term_scrollbar_drag_start_scroll + (delta_y / pixels_per_line);
                        if (new_scroll < 0) new_scroll = 0;
                        if (new_scroll > max_scroll) new_scroll = max_scroll;
                        
                        
                        if (id >= 0 && id < MAX_WIN) {
                            term_states[id].scroll_y = new_scroll;
                            desktop_needs_full_blit = 1;
                        }
                    }
                }
            }
        }
    }
    
    
    {
        int active = win_top_id();
        if (active >= 0 && wins[active].app == APP_NOTEPAD) {
            int cx = wins[active].x + 3;
            int cy = wins[active].y + SVS_TITLE_H + 1;
            int cw = wins[active].w - 6;
            int ch = wins[active].h - SVS_TITLE_H - 3;
            int content_h = ch - 4;
            int scrollbar_w = 11;
            int scroll_x = cx + cw - scrollbar_w - 1;
            
            
            if (notepad_ctx_menu_open) {
                notepad_ctx_menu_hover = hit_notepad_context_menu(mx, my);
                if (left_edge) {
                    notepad_context_menu_click(mx, my, active);
                    prev_buttons = b;
                    desktop_needs_full_blit = 1;
                    return;
                }
                if (left_up && hit_notepad_context_menu(mx, my) < 0) {
                    notepad_ctx_menu_open = 0;
                    notepad_ctx_menu_hover = -1;
                    desktop_needs_full_blit = 1;
                }
            }
            
            
            if (right_edge && !notepad_ctx_menu_open && !ctx_menu_open) {
                if (mx >= cx && mx < scroll_x && my >= cy && my < cy + content_h) {
                    notepad_ctx_menu_x = mx;
                    notepad_ctx_menu_y = my;
                    notepad_ctx_menu_open = 1;
                    notepad_ctx_menu_hover = -1;
                    desktop_needs_full_blit = 1;
                    prev_buttons = b;
                    return;
                }
            }
            
            
            if (left_edge && !click_consumed && menu_open < 0 && mx >= cx && mx < scroll_x && my >= cy && my < cy + content_h) {
                if (notepad_scrollbar_dragging < 0) {
                    handle_notepad_click(active, mx, my, cx, cy, cw, 0);
                }
            }
            
            
            if ((b & 1) && notepad_sel_dragging == active) {
                handle_notepad_drag(active, mx, my, cx, cy, cw);
            }
            
            
            if (left_up && notepad_sel_dragging == active) {
                notepad_end_drag();
            }
        } else {
            
            if (notepad_ctx_menu_open) {
                notepad_ctx_menu_open = 0;
                desktop_needs_full_blit = 1;
            }
        }
    }
    
    
    {
        int active = win_top_id();
        if (active >= 0 && wins[active].app == APP_CALC && left_edge && !click_consumed) {
            int cx = wins[active].x + 3;
            int cy = wins[active].y + SVS_TITLE_H + 1;
            int btn_w = 16;
            int btn_h = 12;
            int gap = 3;
            int display_h = 16;
            int margin = 6;
            int start_x = cx + margin;
            int start_y = cy + margin;
            int btn_area_y = start_y + display_h + 6;
            
            
            if (mx >= start_x && mx < start_x + 4 * (btn_w + gap) &&
                my >= btn_area_y && my < btn_area_y + 5 * (btn_h + gap)) {
                
                
                int col = (mx - start_x) / (btn_w + gap);
                int row = (my - btn_area_y) / (btn_h + gap);
                
                
                if (col < 0) col = 0;
                if (col > 3) col = 3;
                if (row < 0) row = 0;
                if (row > 4) row = 4;
                
                char btn = 0;
                
                
                if (row == 0) {
                    char row0[4] = {'C', 'E', '=', '*'};
                    btn = row0[col];
                } else if (row == 1) {
                    char row1[4] = {'7', '8', '9', '/'};
                    btn = row1[col];
                } else if (row == 2) {
                    char row2[4] = {'4', '5', '6', '-'};
                    btn = row2[col];
                } else if (row == 3) {
                    
                    if (col == 3) {
                        btn = '+'; 
                    } else {
                        char row3[3] = {'1', '2', '3'};
                        btn = row3[col];
                    }
                } else if (row == 4) {
                    
                    if (col <= 1) {
                        btn = '0'; 
                    } else if (col == 2) {
                        btn = '.';
                    } else {
                        btn = 0; 
                    }
                }
                
                
                if (btn >= '0' && btn <= '9') {
                    calc_input_digit(active, btn - '0');
                } else if (btn == '.') {
                    calc_input_decimal(active);
                } else if (btn == 'C') {
                    calc_clear(active);
                } else if (btn == 'E') {
                    
                    calc_clear_entry(active);
                } else if (btn == '=') {
                    calc_equals(active);
                } else if (btn == '+') {
                    calc_set_operation(active, '+');
                } else if (btn == '-') {
                    calc_set_operation(active, '-');
                } else if (btn == '*') {
                    calc_set_operation(active, '*');
                } else if (btn == '/') {
                    calc_set_operation(active, '/');
                }
                desktop_needs_full_blit = 1;
            }
        }
    }

    
    {
        int active = win_top_id();
        if (active >= 0 && wins[active].app == APP_CONTROL_PANEL && left_edge && !click_consumed) {
            int cx = wins[active].x + 3;
            int cy = wins[active].y + SVS_TITLE_H + 1;
            int cw = wins[active].w - 6;
            int ch = wins[active].h - SVS_TITLE_H - 3;
            (void)ch;

            int btn_w = cw - 16;
            if (btn_w < 90) btn_w = 90;
            if (btn_w > cw - 2) btn_w = cw - 2;
            int x = cx + (cw - btn_w) / 2;
            int btn_h = 14;
            int gap = 6;
            int y0 = cy + 30;

            int y1 = y0;
            int y2 = y0 + btn_h + gap;
            int y3 = y2 + btn_h + gap;

            if (mx >= x && mx < x + btn_w) {
                if (my >= y1 && my < y1 + btn_h) {
                    ui_show_scrollbar_grid = !ui_show_scrollbar_grid;
                    desktop_needs_full_blit = 1;
                } else if (my >= y2 && my < y2 + btn_h) {
                    calc_default_scientific = !calc_default_scientific;
                    for (int i = 0; i < MAX_WIN; i++) {
                        if (wins[i].used && wins[i].app == APP_CALC) {
                            calc_scientific[i] = calc_default_scientific;
                            calc_update_display(i);
                        }
                    }
                    desktop_needs_full_blit = 1;
                } else if (my >= y3 && my < y3 + btn_h) {
                    ui_clock_show_seconds = !ui_clock_show_seconds;
                    desktop_needs_full_blit = 1;
                }
            }
        }
    }

    
    {
        int active = win_top_id();
        if (active >= 0 && wins[active].app == APP_PUZZLE && left_edge && !click_consumed) {
            int cx = wins[active].x + 3;
            int cy = wins[active].y + SVS_TITLE_H + 1;
            int cw = wins[active].w - 6;
            int ch = wins[active].h - SVS_TITLE_H - 3;

            
            int header_h = 8 + 8; 
            int board_max_h = ch - header_h - 8 - 12 - 8; 
            if (board_max_h >= 28) {
                int board_size = (cw < board_max_h) ? cw : board_max_h;
                if (board_size > cw - 16) board_size = cw - 16;
                board_size -= board_size % 4;
                if (board_size >= 32) {
                    int bx = cx + (cw - board_size) / 2;
                    int by = cy + header_h;

                    
                    int btn_w = board_size;
                    int btn_h = 12;
                    int btn_x = bx;
                    int btn_y = by + board_size + 8; 
                    if (mx >= btn_x && mx < btn_x + btn_w && my >= btn_y && my < btn_y + btn_h) {
                        puzzle_init(active);
                        desktop_needs_full_blit = 1;
                    } else if (mx >= bx && mx < bx + board_size && my >= by && my < by + board_size) {
                        int tile = board_size / 4;
                        int col = (mx - bx) / tile;
                        int row = (my - by) / tile;
                        if (row >= 0 && row < 4 && col >= 0 && col < 4) {
                            int idx = row * 4 + col;
                            if (puzzle_tiles[active][idx] != 0) {
                                int empty = puzzle_empty_pos[active];
                                int er = empty / 4, ec = empty % 4;
                                int tr = idx / 4, tc = idx % 4;
                                int manhattan = (er > tr ? er - tr : tr - er) + (ec > tc ? ec - tc : tc - ec);
                                if (manhattan == 1) {
                                    puzzle_try_move_tile(active, idx);
                                    desktop_needs_full_blit = 1;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    
    process_drag(mx, my);
    prev_buttons = b;
    
    if (notepad_manual_scroll_timer > 0) notepad_manual_scroll_timer--;
    for (i = 0; i < MAX_WIN; i++) {
        if (wins[i].used && wins[i].animating) { any_animating = 1; break; }
    }
    if (left_edge || left_up || lasso_active || icon_dragging || any_animating ||
        old_hot != menu_hot ||
        old_hover != menu_hover ||
        old_open != menu_open ||
        old_sel != selected_icon ||
        old_ctx_hover != ctx_menu_hover ||
        mx != prev_mx || my != prev_my) {  
        desktop_needs_full_blit = 1;
    }
    
    prev_mx = mx;
    prev_my = my;
}

void wm_draw_all(void) {
    
    vga13_clear_back_buffer();
    
    draw_desktop_background();
    draw_desktop_icons();
    draw_windows_bottom_to_top();
    draw_menu_bar_interactive();
    draw_dropdown_topmost();
    draw_lasso();
    draw_animating_windows();
    draw_context_menu();
    draw_term_context_menu();
    draw_calc_context_menu();
    draw_disk_context_menu();
    draw_notepad_context_menu();
    
    mouse_cursor_draw_to_buffer();
    
    vga13_flip_buffer();
}

void savaos_desktop_run(void) {
    int i;
    ui_show_scrollbar_grid = 0;
    ui_clock_show_seconds = 0;
    calc_default_scientific = 0;
    for (i = 0; i < MAX_WIN; i++) {
        wins[i].used = 0;
        wins[i].animating = 0;
        notepad_has_file[i] = 0;
        notepad_find_has_pattern[i] = 0;
        notepad_find_from_pos[i] = 0;
        calc_scientific[i] = calc_default_scientific;

        puzzle_moves[i] = 0;
        puzzle_empty_pos[i] = 15;
        puzzle_solved[i] = 0;
    }
    z_seq = 0;
    drag_id = -1;
    prev_buttons = 0;
    selected_icon = -1;
    last_click_icon = -1;
    num_selected_icons = 0;
    shift_held = 0;
    icon_dragging = 0;
    icon_drag_idx = -1;
    notepad_scrollbar_dragging = -1;
    notepad_sel_dragging = -1;
    notepad_ctx_menu_open = 0;
    term_scrollbar_dragging = -1;
    term_ctx_menu_open = 0;
    calc_ctx_menu_open = 0;
    disk_ctx_menu_open = 0;
    disk_ctx_target_idx = -1;
    term_clipboard_len = 0;
    calc_clipboard_len = 0;
    term_ctx_menu_hover = -1;
    calc_ctx_menu_hover = -1;
    disk_ctx_menu_hover = -1;
    lasso_active = 0;
    menu_open = -1;
    menu_hover = -1;
    menu_hot = -1;
    ctx_menu_open = 0;
    ctx_menu_hover = -1;
    ctx_menu_target_icon = -1;
    current_menu_app = APP_NONE;
    current_menus = desk_menus;
    current_menu_count = 5;
    menus = desk_menus;

    
    term_init(MAX_WIN);

    vga13_init();
    vga13_init_palette_sv();
    vga13_clear_vram();
    mouse_init();
    kb_set_irq_mode(1);
    irq_system_init();
    layout_menus();

    win_open(APP_ABOUT, "Welcome", 40, 30, 210, 110);
    win_open(APP_NOTEPAD, "ReadMe.txt", 70, 50, 170, 100);
    
    fs_try_mount_fat32();
    
    desktop_mark_icons_dirty();
    desktop_needs_full_blit = 1;

    for (;;) {
        mouse_poll_packets();
        frame_loop();

        if (kb_available()) {
            int c = kb_poll();
            if (c == KEY_ESC) {
                if (ctx_menu_open) {
                    ctx_menu_open = 0;
                    ctx_menu_hover = -1;
                } else if (term_ctx_menu_open) {
                    term_ctx_menu_open = 0;
                    term_ctx_menu_hover = -1;
                } else if (calc_ctx_menu_open) {
                    calc_ctx_menu_open = 0;
                    calc_ctx_menu_hover = -1;
                } else if (disk_ctx_menu_open) {
                    disk_ctx_menu_open = 0;
                    disk_ctx_menu_hover = -1;
                    disk_ctx_target_idx = -1;
                } else if (notepad_ctx_menu_open) {
                    notepad_ctx_menu_open = 0;
                    notepad_ctx_menu_hover = -1;
                } else {
                    menu_open = -1;
                }
            }
            else if (c == KEY_SHIFT) shift_held = !shift_held;
            else {
                int active = win_top_id();
                if (active >= 0) {
                    if (wins[active].app == APP_TERMINAL) handle_terminal_key(active, c);
                    else if (wins[active].app == APP_DISK) handle_disk_key(active, c);
                    else if (wins[active].app == APP_NOTEPAD) handle_notepad_key(active, c);
                    else if (wins[active].app == APP_CALC) handle_calc_key(active, c);
                    else if (wins[active].app == APP_PUZZLE) handle_puzzle_key(active, c);
                }
            }
            desktop_needs_full_blit = 1;
        }

        __asm__ volatile ("cli");
        if (desktop_needs_full_blit) {
            int was_animating = any_window_animating();
            wm_draw_all();
            desktop_needs_full_blit = 0;
            if (any_window_animating()) {
                __asm__ volatile ("sti");
                sleep_ms(WIN_ANIM_DELAY_MS);
                desktop_needs_full_blit = 1;
            } else if (was_animating) {
                wm_draw_all();
                __asm__ volatile ("sti");
                __asm__ volatile ("hlt");
            } else {
                __asm__ volatile ("sti");
                __asm__ volatile ("hlt");
            }
        } else {
            __asm__ volatile ("sti");
            __asm__ volatile ("hlt");
        }
    }
}
